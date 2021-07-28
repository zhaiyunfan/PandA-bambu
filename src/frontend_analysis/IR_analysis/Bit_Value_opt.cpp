/*
 *
 *                   _/_/_/    _/_/   _/    _/ _/_/_/    _/_/
 *                  _/   _/ _/    _/ _/_/  _/ _/   _/ _/    _/
 *                 _/_/_/  _/_/_/_/ _/  _/_/ _/   _/ _/_/_/_/
 *                _/      _/    _/ _/    _/ _/   _/ _/    _/
 *               _/      _/    _/ _/    _/ _/_/_/  _/    _/
 *
 *             ***********************************************
 *                              PandA Project
 *                     URL: http://panda.dei.polimi.it
 *                       Politecnico di Milano - DEIB
 *                        System Architectures Group
 *             ***********************************************
 *              Copyright (C) 2004-2021 Politecnico di Milano
 *
 *   This file is part of the PandA framework.
 *
 *   The PandA framework is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

/**
 * @file Bit_Value_opt.cpp
 * @brief
 *
 * @author Fabrizio Ferrandi <fabrizio.ferrandi@polimi.it>
 * $Revision$
 * $Date$
 * Last modified by $Author$
 *
 */

// Header include
#include "Bit_Value_opt.hpp"

#include "Range.hpp"
#include "bit_lattice.hpp"

/// Autoheader include
#include "config_HAVE_FROM_DISCREPANCY_BUILT.hpp"
#include "config_HAVE_STDCXX_17.hpp"

// Behavior include
#include "application_manager.hpp"
#include "behavioral_helper.hpp"
#include "call_graph.hpp"
#include "call_graph_manager.hpp"
#include "function_behavior.hpp"

/// HLS/vcd include
#include "Discrepancy.hpp"

// Parameter include
#include "Parameter.hpp"

/// design_flows includes
#include "design_flow_graph.hpp"
#include "design_flow_manager.hpp"
#include "frontend_flow_step_factory.hpp"

// STD include
#include <boost/range/adaptor/reversed.hpp>
#include <cmath>
#include <fstream>
#include <string>

// Tree include
#include "dbgPrintHelper.hpp" // for DEBUG_LEVEL_
#include "ext_tree_node.hpp"
#include "hls_manager.hpp"
#include "hls_target.hpp"
#include "math_function.hpp"       // for resize_to_1_8_16_32_64_128_256_512
#include "string_manipulation.hpp" // for GET_CLASS
#include "tree_basic_block.hpp"
#include "tree_helper.hpp"
#include "tree_manager.hpp"
#include "tree_manipulation.hpp"
#include "tree_node.hpp"
#include "tree_reindex.hpp"
#include "utility.hpp"

Bit_Value_opt::Bit_Value_opt(const ParameterConstRef _parameters, const application_managerRef _AppM, unsigned int _function_id, const DesignFlowManagerConstRef _design_flow_manager)
    : FunctionFrontendFlowStep(_AppM, _function_id, BIT_VALUE_OPT, _design_flow_manager, _parameters), modified(false)
{
   debug_level = parameters->get_class_debug_level(GET_CLASS(*this), DEBUG_LEVEL_NONE);
}

Bit_Value_opt::~Bit_Value_opt() = default;

const CustomUnorderedSet<std::pair<FrontendFlowStepType, FrontendFlowStep::FunctionRelationship>> Bit_Value_opt::ComputeFrontendRelationships(const DesignFlowStep::RelationshipType relationship_type) const
{
   CustomUnorderedSet<std::pair<FrontendFlowStepType, FunctionRelationship>> relationships;
   switch(relationship_type)
   {
      case DEPENDENCE_RELATIONSHIP:
      {
         relationships.insert(std::make_pair(PARM2SSA, SAME_FUNCTION));
         relationships.insert(std::make_pair(USE_COUNTING, SAME_FUNCTION));
         if(!parameters->getOption<int>(OPT_gcc_openmp_simd))
         {
            relationships.insert(std::make_pair(BIT_VALUE, SAME_FUNCTION));
            if(parameters->isOption(OPT_bitvalue_ipa) && parameters->getOption<bool>(OPT_bitvalue_ipa))
            {
               relationships.insert(std::make_pair(BIT_VALUE_IPA, WHOLE_APPLICATION));
            }
         }
         relationships.insert(std::make_pair(BIT_VALUE_OPT, CALLED_FUNCTIONS));
         break;
      }
      case(PRECEDENCE_RELATIONSHIP):
      {
         break;
      }
      case(INVALIDATION_RELATIONSHIP):
      {
         if(GetStatus() == DesignFlowStep_Status::SUCCESS)
         {
            if(!parameters->getOption<int>(OPT_gcc_openmp_simd))
            {
               relationships.insert(std::make_pair(BIT_VALUE, SAME_FUNCTION));
            }
         }
         break;
      }
      default:
         THROW_UNREACHABLE("");
   }
   return relationships;
}

bool Bit_Value_opt::HasToBeExecuted() const
{
   return FunctionFrontendFlowStep::HasToBeExecuted() || bitvalue_version != function_behavior->GetBitValueVersion();
}

DesignFlowStep_Status Bit_Value_opt::InternalExec()
{
   PRINT_DBG_MEX(DEBUG_LEVEL_PEDANTIC, debug_level, " --------- BIT_VALUE_OPT ----------");
   const auto TM = AppM->get_tree_manager();
   const auto IRman = tree_manipulationRef(new tree_manipulation(TM, parameters, AppM));

   const auto tn = TM->CGetTreeNode(function_id);
   // tree_nodeRef Scpe = TM->GetTreeReindex(function_id);
   const auto fd = GetPointerS<const function_decl>(tn);
   THROW_ASSERT(fd && fd->body, "Node is not a function or it hasn't a body");
   modified = false;
   optimize(fd, TM, IRman);
   modified ? function_behavior->UpdateBBVersion() : 0;
   return modified ? DesignFlowStep_Status::SUCCESS : DesignFlowStep_Status::UNCHANGED;
}

void Bit_Value_opt::constrainSSA(ssa_name* op_ssa, tree_managerRef TM)
{
   if(tree_helper::is_real(TM, GET_INDEX_CONST_NODE(op_ssa->type)))
   {
      return;
   }
   const auto nbit = op_ssa->bit_values.size();
   const auto op0_type_id = GET_INDEX_CONST_NODE(op_ssa->type);
   const auto nbitType = BitLatticeManipulator::size(TM, op0_type_id);
   if(nbit != nbitType)
   {
      const bool isSigned = tree_helper::is_int(TM, op0_type_id);
      if(isSigned)
      {
         RangeRef constraintRange(new Range(Regular, static_cast<Range::bw_t>(nbitType), -(1ll << (nbit - 1)), (1ll << (nbit - 1)) - 1));
         if(op_ssa->range)
         {
            if(op_ssa->range->getSpan() < constraintRange->getSpan())
            {
               return;
            }
         }
         else
         {
            op_ssa->range = constraintRange;
         }
         op_ssa->min = TM->CreateUniqueIntegerCst(-(1ll << (nbit - 1)), op0_type_id);
         op_ssa->max = TM->CreateUniqueIntegerCst((1ll << (nbit - 1)) - 1, op0_type_id);
      }
      else
      {
         RangeRef constraintRange(new Range(Regular, static_cast<Range::bw_t>(nbitType), 0, (1ll << nbit) - 1));
         if(op_ssa->range)
         {
            if(op_ssa->range->getSpan() < constraintRange->getSpan())
            {
               return;
            }
         }
         else
         {
            op_ssa->range = constraintRange;
         }
         op_ssa->min = TM->CreateUniqueIntegerCst(0, op0_type_id);
         op_ssa->max = TM->CreateUniqueIntegerCst((1ll << nbit) - 1, op0_type_id);
      }
      // std::cerr<<"var " << op_ssa->ToString()<<" ";
      // std::cerr << "min " <<op_ssa->min->ToString() << " max " <<op_ssa->max->ToString()<<"\n";
      // std::cerr << "nbit "<< nbit << " nbitType " << nbitType <<"\n";
   }
}

static unsigned long long int convert_bitvalue2longlong(const std::string& bit_values, tree_managerRef TM, unsigned int var_id)
{
   unsigned long long int const_value = 0;
   unsigned int index_val = 0;
   for(auto current_el : boost::adaptors::reverse(bit_values))
   {
      if(current_el == '1')
      {
         const_value |= 1ULL << index_val;
      }
      ++index_val;
   }
   /// in case do sign extension
   if(tree_helper::is_int(TM, var_id) && bit_values[0] == '1')
   {
      for(; index_val < 64; ++index_val)
      {
         const_value |= 1ULL << index_val;
      }
   }
   return const_value;
}

static bool is_bit_values_constant(const std::string& bit_values)
{
   bool is_constant = bit_values.size() != 0;
   for(auto current_el : bit_values)
   {
      if(current_el == 'U')
      {
         is_constant = false;
         break;
      }
   }
   return is_constant;
}

void Bit_Value_opt::propagateValue(const ssa_name* ssa, tree_managerRef TM, tree_nodeRef old_val, tree_nodeRef new_val)
{
   THROW_ASSERT(tree_helper::Size(tree_helper::CGetType(GET_CONST_NODE(old_val))) == tree_helper::Size(tree_helper::CGetType(GET_CONST_NODE(new_val))), "unexpected case " + STR(old_val) + " " + STR(new_val));
   const auto StmtUses = ssa->CGetUseStmts();
   for(const auto& use : StmtUses)
   {
      if(!AppM->ApplyNewTransformation())
      {
         break;
      }
      INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace var usage before: " + use.first->ToString());
      TM->ReplaceTreeNode(use.first, old_val, new_val);
      INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace var usage after: " + use.first->ToString());
      modified = true;
      AppM->RegisterTransformation(GetName(), use.first);
   }
}

void Bit_Value_opt::optimize(const function_decl* fd, tree_managerRef TM, tree_manipulationRef IRman)
{
   /// in case propagate constants from parameters
   for(const auto& parm_decl_node : fd->list_of_args)
   {
      const unsigned int p_decl_id = AppM->getSSAFromParm(function_id, GET_INDEX_CONST_NODE(parm_decl_node));
      if(tree_helper::is_real(TM, p_decl_id) || tree_helper::is_a_complex(TM, p_decl_id))
      {
         INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---Parameter not supported " + TM->get_tree_node_const(p_decl_id)->ToString());
         continue;
      }
      if(AppM->ApplyNewTransformation())
      {
         const auto parm_type = tree_helper::CGetType(GET_NODE(parm_decl_node));
         const auto parmssa = TM->CGetTreeNode(p_decl_id);
         const auto p = GetPointer<const ssa_name>(parmssa);
         const auto is_constant = is_bit_values_constant(p->bit_values);
         if(is_constant)
         {
            const auto ull_value = convert_bitvalue2longlong(p->bit_values, TM, p_decl_id);
            const auto val = TM->CreateUniqueIntegerCst(static_cast<long long int>(ull_value), parm_type->index);
            propagateValue(p, TM, TM->CGetTreeReindex(p_decl_id), val);
         }
      }
   }
   auto sl = GetPointerS<statement_list>(GET_NODE(fd->body));
   THROW_ASSERT(sl, "Body is not a statement_list");
   for(const auto& bb_pair : sl->list_of_bloc)
   {
      const auto B = bb_pair.second;
      const auto B_id = B->number;
      INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "-->Examining BB" + STR(B_id));
      const auto list_of_stmt = B->CGetStmtList();
      for(const auto& stmt : list_of_stmt)
      {
         INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "-->Examining statement " + GET_NODE(stmt)->ToString());
         if(!AppM->ApplyNewTransformation())
         {
            INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "<--Skipped because reached limit of CFG transformations");
            break;
         }
         if(GetPointerS<gimple_node>(GET_NODE(stmt))->keep)
         {
            INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "<--Skipped because the statement has been annotated with the keep tag");
            continue;
         }
         if(GET_NODE(stmt)->get_kind() == gimple_assign_K)
         {
            auto ga = GetPointerS<gimple_assign>(GET_NODE(stmt));
            unsigned int output_uid = GET_INDEX_CONST_NODE(ga->op0);
            auto ssa = GetPointer<ssa_name>(GET_NODE(ga->op0));
            if(ssa && !ssa->bit_values.empty() && !ssa->CGetUseStmts().empty())
            {
               if(tree_helper::is_real(TM, output_uid))
               {
                  auto real_BVO = [&] {
                     if(GET_NODE(ga->op1)->get_kind() == cond_expr_K)
                     {
                        const auto me = GetPointerS<cond_expr>(GET_NODE(ga->op1));
                        const auto op0 = GET_NODE(me->op1);
                        const auto op1 = GET_NODE(me->op2);
                        const auto condition = GET_NODE(me->op0);
                        if(op0 == op1)
                        {
                           INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---Cond expr with equal operands");
                           propagateValue(ssa, TM, ga->op0, me->op1);
                        }
                        else if(condition->get_kind() == integer_cst_K)
                        {
                           INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---Cond expr with constant condition");
                           const auto ic = GetPointerS<integer_cst>(condition);
                           auto ull_value = static_cast<unsigned long long int>(tree_helper::get_integer_cst_value(ic));
                           tree_nodeRef new_val = ull_value ? me->op1 : me->op2;
                           propagateValue(ssa, TM, ga->op0, new_val);
                        }
                        else
                        {
                           INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---nothing more can be done");
                        }
                     }
                     else if(GetPointer<cst_node>(GET_NODE(ga->op1)))
                     {
                        propagateValue(ssa, TM, ga->op0, ga->op1);
                     }
                     else if(GetPointer<ssa_name>(GET_NODE(ga->op1)))
                     {
                        propagateValue(ssa, TM, ga->op0, ga->op1);
                     }
                     else if(GET_NODE(ga->op1)->get_kind() == view_convert_expr_K)
                     {
                        auto vce = GetPointerS<view_convert_expr>(GET_NODE(ga->op1));
                        if(GetPointer<cst_node>(GET_NODE(vce->op)))
                        {
                           if(GET_NODE(vce->op)->get_kind() == integer_cst_K)
                           {
                              auto* int_const = GetPointerS<integer_cst>(GET_NODE(vce->op));
                              auto bitwidth_op = BitLatticeManipulator::Size(vce->type);
                              tree_nodeRef val;
                              if(bitwidth_op == 32)
                              {
                                 union
                                 {
                                    float dest;
                                    int source;
                                 } __conv_union;
                                 __conv_union.source = static_cast<int>(int_const->value);
                                 const auto data_value_id = TM->new_tree_node_id();
                                 const tree_manipulationRef tree_man = tree_manipulationRef(new tree_manipulation(TM, parameters, AppM));
                                 val = tree_man->CreateRealCst(vce->type, static_cast<long double>(__conv_union.dest), data_value_id);
                              }
                              else if(bitwidth_op == 64)
                              {
                                 union
                                 {
                                    double dest;
                                    long long int source;
                                 } __conv_union;
                                 __conv_union.source = int_const->value;
                                 const auto data_value_id = TM->new_tree_node_id();
                                 const tree_manipulationRef tree_man = tree_manipulationRef(new tree_manipulation(TM, parameters, AppM));
                                 val = tree_man->CreateRealCst(vce->type, static_cast<long double>(__conv_union.dest), data_value_id);
                              }
                              else
                              {
                                 THROW_ERROR("not supported floating point bitwidth");
                              }
                              propagateValue(ssa, TM, ga->op0, val);
                           }
                        }
                     }
                     else
                     {
                        INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---real variables not considered: " + STR(GET_INDEX_CONST_NODE(ga->op0)));
                     }
                     INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "<--Examined statement " + GET_NODE(stmt)->ToString());
                  };
                  real_BVO();
                  continue;
               }
               if(tree_helper::is_a_complex(TM, output_uid))
               {
                  INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---complex variables not considered: " + STR(GET_INDEX_CONST_NODE(ga->op0)));
                  INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "<--Examined statement " + GET_NODE(stmt)->ToString());
                  continue;
               }
               if(tree_helper::is_a_vector(TM, output_uid))
               {
                  INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---vector variables not considered: " + STR(GET_INDEX_CONST_NODE(ga->op0)));
                  INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "<--Examined statement " + GET_NODE(stmt)->ToString());
                  continue;
               }
               if((GetPointer<integer_cst>(GET_NODE(ga->op1)) || GetPointer<real_cst>(GET_NODE(ga->op1))) && tree_helper::is_a_pointer(TM, GET_INDEX_CONST_NODE(ga->op1)))
               {
                  INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---constant pointer value assignments not considered: " + STR(GET_INDEX_CONST_NODE(ga->op0)));
                  INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "<--Examined statement " + GET_NODE(stmt)->ToString());
                  continue;
               }
               if(GetPointer<call_expr>(GET_NODE(ga->op1)) and ga->vdef)
               {
                  INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---calls with side effects cannot be optimized" + STR(GET_INDEX_CONST_NODE(ga->op1)));
                  INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "<--Examined statement " + GET_NODE(stmt)->ToString());
                  continue;
               }
               if(GetPointer<addr_expr>(GET_NODE(ga->op1)))
               {
                  INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---addr_expr cannot be optimized" + STR(GET_INDEX_CONST_NODE(ga->op1)));
                  INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "<--Examined statement " + GET_NODE(stmt)->ToString());
                  continue;
               }
               auto ga_BVO = [&] {
                  const auto ga_op_type = TM->CGetTreeReindex(tree_helper::CGetType(GET_CONST_NODE(ga->op0))->index);
                  const auto Scpe = TM->GetTreeReindex(function_id);
                  const auto& bit_values = ssa->bit_values;
                  auto is_constant = is_bit_values_constant(bit_values) && !tree_helper::is_a_pointer(TM, GET_INDEX_CONST_NODE(ga->op1));
                  auto rel_expr_BVO = [&] {
                     auto* me = GetPointer<binary_expr>(GET_NODE(ga->op1));
                     const auto op0 = GET_CONST_NODE(me->op0);
                     const auto op1 = GET_CONST_NODE(me->op1);

                     std::string s0, s1;
                     if(GetPointer<const ssa_name>(op0))
                     {
                        s0 = GetPointer<const ssa_name>(op0)->bit_values;
                     }
                     if(GetPointer<const ssa_name>(op1))
                     {
                        s1 = GetPointer<const ssa_name>(op1)->bit_values;
                     }
                     unsigned int precision;
                     if(s0.size() && s1.size())
                     {
                        precision = static_cast<unsigned int>(std::min(s0.size(), s1.size()));
                     }
                     else
                     {
                        precision = static_cast<unsigned int>(std::max(s0.size(), s1.size()));
                     }

                     if(precision)
                     {
                        unsigned int trailing_zero = 0;
                        if(GetPointer<const integer_cst>(op0))
                        {
                           auto* ic = GetPointer<const integer_cst>(op0);
                           auto ull_value = static_cast<unsigned long long int>(tree_helper::get_integer_cst_value(ic));
                           s0 = convert_to_binary(ull_value, precision);
                        }
                        if(GetPointer<const integer_cst>(op1))
                        {
                           auto* ic = GetPointer<const integer_cst>(op1);
                           auto ull_value = static_cast<unsigned long long int>(tree_helper::get_integer_cst_value(ic));
                           s1 = convert_to_binary(ull_value, precision);
                        }
                        precision = static_cast<unsigned int>(std::min(s0.size(), s1.size()));
                        if(precision == 0)
                        {
                           precision = 1;
                        }
                        for(auto s0it = s0.rbegin(), s1it = s1.rbegin(), s0end = s0.rend(), s1end = s1.rend(); s0it != s0end && s1it != s1end; ++s0it, ++s1it)
                        {
                           if((*s0it == *s1it && (*s1it == '0' || *s1it == '1')) || *s0it == 'X' || *s1it == 'X')
                           {
                              ++trailing_zero;
                           }
                           else
                           {
                              break;
                           }
                        }
                        if(trailing_zero)
                        {
                           INDENT_OUT_MEX(OUTPUT_LEVEL_VERBOSE, output_level, "-->Bit Value Opt: " + std::string(GET_NODE(ga->op1)->get_kind_text()) + " optimized, nbits = " + STR(trailing_zero));
                           INDENT_OUT_MEX(OUTPUT_LEVEL_VERBOSE, output_level, "<--");
                           modified = true;
                           AppM->RegisterTransformation(GetName(), stmt);
                           const auto srcp_default = ga->include_name + ":" + STR(ga->line_number) + ":" + STR(ga->column_number);
                           const auto op0_op_type = TM->CGetTreeReindex(tree_helper::CGetType(op0)->index);
                           const auto op1_op_type = TM->CGetTreeReindex(tree_helper::CGetType(op1)->index);

                           if(GetPointer<const ssa_name>(op0))
                           {
                              const auto op0_const_node = TM->CreateUniqueIntegerCst(static_cast<long long int>(trailing_zero), GET_INDEX_CONST_NODE(op0_op_type));
                              const auto op0_expr = IRman->create_binary_operation(op0_op_type, me->op0, op0_const_node, srcp_default, rshift_expr_K);
                              const auto op0_ga = IRman->CreateGimpleAssign(op0_op_type, nullptr, nullptr, op0_expr, function_id, B_id, srcp_default);
                              INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "Created " + STR(op0_ga));
                              B->PushBefore(op0_ga, stmt, AppM);
                              const auto op0_ga_var = GetPointer<const gimple_assign>(GET_CONST_NODE(op0_ga))->op0;
                              TM->ReplaceTreeNode(stmt, me->op0, op0_ga_var);
                              /// set the bit_values to the ssa var
                              auto* op0_ssa = GetPointer<ssa_name>(GET_NODE(op0_ga_var));
                              op0_ssa->bit_values = GetPointer<ssa_name>(op0)->bit_values.substr(0, GetPointer<ssa_name>(op0)->bit_values.size() - trailing_zero);
                              constrainSSA(op0_ssa, TM);
                           }
                           else
                           {
                              const auto int_const = GetPointer<const integer_cst>(op0);
                              if(tree_helper::is_int(TM, op0->index))
                              {
                                 TM->ReplaceTreeNode(stmt, me->op0, TM->CreateUniqueIntegerCst(static_cast<long long int>(int_const->value >> trailing_zero), GET_INDEX_CONST_NODE(op0_op_type)));
                              }
                              else
                              {
                                 TM->ReplaceTreeNode(stmt, me->op0, TM->CreateUniqueIntegerCst(static_cast<long long int>(static_cast<unsigned long long int>(int_const->value) >> trailing_zero), GET_INDEX_CONST_NODE(op0_op_type)));
                              }
                           }
                           if(GetPointer<const ssa_name>(op1))
                           {
                              const auto op1_const_node = TM->CreateUniqueIntegerCst(static_cast<long long int>(trailing_zero), GET_INDEX_CONST_NODE(op1_op_type));
                              const auto op1_expr = IRman->create_binary_operation(op1_op_type, me->op1, op1_const_node, srcp_default, rshift_expr_K);
                              const auto op1_ga = IRman->CreateGimpleAssign(op1_op_type, nullptr, nullptr, op1_expr, function_id, B_id, srcp_default);
                              INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "Created " + STR(op1_ga));
                              B->PushBefore(op1_ga, stmt, AppM);
                              const auto op1_ga_var = GetPointer<const gimple_assign>(GET_CONST_NODE(op1_ga))->op0;
                              TM->ReplaceTreeNode(stmt, me->op1, op1_ga_var);
                              /// set the bit_values to the ssa var
                              const auto op1_ssa = GetPointer<ssa_name>(GET_NODE(op1_ga_var));
                              op1_ssa->bit_values = GetPointer<ssa_name>(op1)->bit_values.substr(0, GetPointer<ssa_name>(op1)->bit_values.size() - trailing_zero);
                              constrainSSA(op1_ssa, TM);
                           }
                           else
                           {
                              const auto int_const = GetPointer<const integer_cst>(op1);
                              if(tree_helper::is_int(TM, op1->index))
                              {
                                 TM->ReplaceTreeNode(stmt, me->op1, TM->CreateUniqueIntegerCst(static_cast<long long int>(int_const->value >> trailing_zero), GET_INDEX_CONST_NODE(op1_op_type)));
                              }
                              else
                              {
                                 TM->ReplaceTreeNode(stmt, me->op1, TM->CreateUniqueIntegerCst(static_cast<long long int>(static_cast<unsigned long long int>(int_const->value) >> trailing_zero), GET_INDEX_CONST_NODE(op1_op_type)));
                              }
                           }
                        }
                     }
                  };

                  if(is_constant)
                  {
                     auto c_BVO = [&] {
                        INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---Left part is constant " + bit_values);
                        tree_nodeRef val;
                        if(GetPointer<const integer_cst>(GET_CONST_NODE(ga->op1)))
                        {
                           val = ga->op1;
                        }
                        else
                        {
                           const auto const_value = convert_bitvalue2longlong(bit_values, TM, output_uid);
                           val = TM->CreateUniqueIntegerCst(static_cast<long long int>(const_value), GET_INDEX_CONST_NODE(ga_op_type));
                        }
                        if(AppM->ApplyNewTransformation())
                        {
                           if(GET_CONST_NODE(ga->op0)->get_kind() == ssa_name_K && ga->predicate)
                           {
                              if(GET_CONST_NODE(ga->predicate)->get_kind() != integer_cst_K || GetPointer<const integer_cst>(GET_CONST_NODE(ga->predicate))->value != 0)
                              {
                                 INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---zero predicated statement: " + stmt->ToString());
                                 const auto bt = IRman->create_boolean_type();
                                 const auto zeroval = TM->CreateUniqueIntegerCst(static_cast<long long int>(0), bt->index);
                                 TM->ReplaceTreeNode(stmt, ga->predicate, zeroval);
                                 modified = true;
                                 AppM->RegisterTransformation(GetName(), stmt);
                              }
                           }
                        }
                        propagateValue(ssa, TM, ga->op0, val);
                     };
                     c_BVO();
                  }
                  else if(GetPointer<const cst_node>(GET_CONST_NODE(ga->op1)))
                  {
                     propagateValue(ssa, TM, ga->op0, ga->op1);
                  }
                  else if(GetPointer<const ssa_name>(GET_CONST_NODE(ga->op1)))
                  {
                     auto ssa_name_BVO = [&] {
                        if(!ssa->bit_values.empty() && ssa->bit_values.at(0) == '0' && ssa->bit_values.size() <= 64)
                        {
                           const auto srcp_default = ga->include_name + ":" + STR(ga->line_number) + ":" + STR(ga->column_number);
                           const auto bit_mask_constant_node = TM->CreateUniqueIntegerCst((1LL << (ssa->bit_values.size() - 1)) - 1, GET_INDEX_CONST_NODE(ssa->type));
                           const auto band_expr = IRman->create_binary_operation(ssa->type, ga->op1, bit_mask_constant_node, srcp_default, bit_and_expr_K);
                           INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace ssa usage before: " + stmt->ToString());
                           TM->ReplaceTreeNode(stmt, ga->op1, band_expr);
                           INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace ssa usage after: " + stmt->ToString());
                           modified = true;
                           ga->keep = true; /// this prevent an infinite loop with CSE
                        }
                        else
                        {
                           auto bw_op1 = BitLatticeManipulator::Size(GET_CONST_NODE(ga->op1));
                           auto bw_op0 = BitLatticeManipulator::Size(GET_CONST_NODE(ga->op0));
                           auto max_bw = 0u;
                           for(const auto& use : ssa->CGetUseStmts())
                           {
                              if(GET_CONST_NODE(use.first)->get_kind() == gimple_assign_K && GET_CONST_NODE(GetPointerS<const gimple_assign>(GET_CONST_NODE(use.first))->op1)->get_kind() == ssa_name_K)
                              {
                                 max_bw = std::max(max_bw, BitLatticeManipulator::Size(GET_CONST_NODE(GetPointerS<const gimple_assign>(GET_CONST_NODE(use.first))->op1)));
                              }
                              else
                              {
                                 max_bw = bw_op1;
                              }
                           }
                           if(max_bw < bw_op1)
                           {
                              auto ssa1 = GetPointer<ssa_name>(GET_NODE(ga->op1));
                              ssa1->min = ssa->min;
                              ssa1->max = ssa->max;
                              bw_op1 = BitLatticeManipulator::Size(GET_CONST_NODE(ga->op1));
                           }

                           if(bw_op1 <= bw_op0)
                           {
                              propagateValue(ssa, TM, ga->op0, ga->op1);
                           }
                        }
                     };
                     ssa_name_BVO();
                  }
                  else if(GET_CONST_NODE(ga->op1)->get_kind() == mult_expr_K || GET_CONST_NODE(ga->op1)->get_kind() == widen_mult_expr_K)
                  {
                     auto mult_expr_BVO = [&] {
                        const auto me = GetPointer<const binary_expr>(GET_CONST_NODE(ga->op1));
                        const auto op0 = GET_CONST_NODE(me->op0);
                        const auto op1 = GET_CONST_NODE(me->op1);
                        /// first check if we have to change a mult_expr in a widen_mult_expr
                        const auto data_bitsize_out = resize_to_1_8_16_32_64_128_256_512(BitLatticeManipulator::Size(GET_CONST_NODE(ga->op0)));
                        const auto data_bitsize_in0 = resize_to_1_8_16_32_64_128_256_512(BitLatticeManipulator::Size(op0));
                        const auto data_bitsize_in1 = resize_to_1_8_16_32_64_128_256_512(BitLatticeManipulator::Size(op1));
                        const auto realp = tree_helper::is_real(TM, GET_INDEX_CONST_NODE(GetPointer<const binary_expr>(GET_CONST_NODE(ga->op1))->type));
                        if(GET_CONST_NODE(ga->op1)->get_kind() == mult_expr_K && !realp && std::max(data_bitsize_in0, data_bitsize_in1) * 2 == data_bitsize_out)
                        {
                           const auto srcp_default = ga->include_name + ":" + STR(ga->line_number) + ":" + STR(ga->column_number);
                           const auto ga_op1 = GetPointer<const binary_expr>(GET_CONST_NODE(ga->op1));
                           const auto new_widen_expr = IRman->create_binary_operation(ga_op_type, ga_op1->op0, ga_op1->op1, srcp_default, widen_mult_expr_K);
                           INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---Replacing " + STR(ga->op1) + " with " + STR(new_widen_expr) + " in " + STR(stmt));
                           modified = true;
                           TM->ReplaceTreeNode(stmt, ga->op1, new_widen_expr);
                           INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace expression with a widen mult_expr: " + stmt->ToString());
                        }
                        else if(GET_CONST_NODE(ga->op1)->get_kind() == widen_mult_expr_K && !realp && std::max(data_bitsize_in0, data_bitsize_in1) == data_bitsize_out)
                        {
                           const auto srcp_default = ga->include_name + ":" + STR(ga->line_number) + ":" + STR(ga->column_number);
                           const auto ga_op1 = GetPointer<const binary_expr>(GET_CONST_NODE(ga->op1));
                           auto res_type = TM->CGetTreeReindex(tree_helper::CGetType(GET_CONST_NODE(ga_op1->op0))->index);
                           const auto new_expr = IRman->create_binary_operation(res_type, ga_op1->op0, ga_op1->op1, srcp_default, mult_expr_K);
                           const auto op0_ga = IRman->CreateGimpleAssign(res_type, nullptr, nullptr, new_expr, function_id, B_id, srcp_default);
                           B->PushBefore(op0_ga, stmt, AppM);
                           INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "Created " + STR(op0_ga));
                           const auto op0_ga_var = GetPointer<const gimple_assign>(GET_CONST_NODE(op0_ga))->op0;
                           const auto nop_expr_node = IRman->create_unary_operation(ga_op_type, op0_ga_var, srcp_default, nop_expr_K);
                           INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---Replacing " + STR(ga->op1) + " with " + STR(op0_ga_var) + " in " + STR(stmt));
                           modified = true;
                           TM->ReplaceTreeNode(stmt, ga->op1, nop_expr_node);
                           INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace expression with a mult_expr: " + stmt->ToString());
                        }
                        const auto isSigned = tree_helper::is_int(TM, GET_INDEX_CONST_NODE(ga_op_type));
                        if(!isSigned && GET_CONST_NODE(ga->op1)->get_kind() == mult_expr_K && (data_bitsize_in0 == 1 || data_bitsize_in1 == 1))
                        {
                           modified = true;
                           AppM->RegisterTransformation(GetName(), stmt);
                           const auto constNE0 = TM->CreateUniqueIntegerCst(0LL, GET_INDEX_CONST_NODE(ga_op_type));
                           const auto srcp_default = ga->include_name + ":" + STR(ga->line_number) + ":" + STR(ga->column_number);
                           const auto bt = IRman->create_boolean_type();
                           const auto cond_op0 = IRman->create_binary_operation(bt, data_bitsize_in0 == 1 ? me->op0 : me->op1, constNE0, srcp_default, ne_expr_K);
                           const auto op0_ga = IRman->CreateGimpleAssign(bt, TM->CreateUniqueIntegerCst(0LL, bt->index), TM->CreateUniqueIntegerCst(1LL, bt->index), cond_op0, function_id, B_id, srcp_default);
                           INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "Created " + STR(op0_ga));
                           B->PushBefore(op0_ga, stmt, AppM);
                           const auto op0_ga_var = GetPointer<const gimple_assign>(GET_CONST_NODE(op0_ga))->op0;
                           const auto const0 = TM->CreateUniqueIntegerCst(0LL, GET_INDEX_CONST_NODE(ga_op_type));
                           const auto cond_op = IRman->create_ternary_operation(ga_op_type, op0_ga_var, data_bitsize_in1 == 1 ? me->op0 : me->op1, const0, srcp_default, cond_expr_K);
                           THROW_ASSERT(tree_helper::CGetType(GET_CONST_NODE(GetPointer<const gimple_assign>(GET_CONST_NODE(stmt))->op0))->index == GET_INDEX_CONST_NODE(ga_op_type), "");
                           INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---Replacing " + STR(ga->op1) + " with " + STR(cond_op) + " in " + STR(stmt));
                           TM->ReplaceTreeNode(stmt, ga->op1, cond_op);
                           INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace expression with a cond_expr: " + stmt->ToString());
                        }
                        else
                        {
                           unsigned int trailing_zero_op0 = 0;
                           unsigned int trailing_zero_op1 = 0;
                           if(GetPointer<const ssa_name>(op0))
                           {
                              const auto& bit_values_op0 = GetPointer<const ssa_name>(op0)->bit_values;
                              for(auto current_el : boost::adaptors::reverse(bit_values_op0))
                              {
                                 if(current_el == '0' || current_el == 'X')
                                 {
                                    ++trailing_zero_op0;
                                 }
                                 else
                                 {
                                    break;
                                 }
                              }
                           }
                           else if(GetPointer<const integer_cst>(op0))
                           {
                              const auto int_const = GetPointer<const integer_cst>(op0);
                              const auto value_int = static_cast<unsigned long long int>(int_const->value);
                              for(unsigned int index = 0; index < 64 && value_int != 0; ++index)
                              {
                                 if(value_int & (1ULL << index))
                                 {
                                    break;
                                 }
                                 else
                                 {
                                    ++trailing_zero_op0;
                                 }
                              }
                           }
                           if(GetPointer<const ssa_name>(op1))
                           {
                              const auto& bit_values_op1 = GetPointer<const ssa_name>(op1)->bit_values;
                              for(auto current_el : boost::adaptors::reverse(bit_values_op1))
                              {
                                 if(current_el == '0' || current_el == 'X')
                                 {
                                    ++trailing_zero_op1;
                                 }
                                 else
                                 {
                                    break;
                                 }
                              }
                           }
                           else if(GetPointer<const integer_cst>(op1))
                           {
                              const auto int_const = GetPointer<const integer_cst>(op1);
                              const auto value_int = static_cast<unsigned long long int>(int_const->value);
                              for(unsigned int index = 0; index < 64 && value_int != 0; ++index)
                              {
                                 if(value_int & (1ULL << index))
                                 {
                                    break;
                                 }
                                 else
                                 {
                                    ++trailing_zero_op1;
                                 }
                              }
                           }
                           if(trailing_zero_op0 != 0 || trailing_zero_op1 != 0)
                           {
                              modified = true;
                              AppM->RegisterTransformation(GetName(), stmt);
                              INDENT_OUT_MEX(OUTPUT_LEVEL_VERBOSE, output_level, "-->Bit Value Opt: mult_expr/widen_mult_expr optimized, nbits = " + STR(trailing_zero_op0 + trailing_zero_op1));
                              INDENT_OUT_MEX(OUTPUT_LEVEL_VERBOSE, output_level, "<--");
                              const auto srcp_default = ga->include_name + ":" + STR(ga->line_number) + ":" + STR(ga->column_number);
                              if(trailing_zero_op0 != 0)
                              {
                                 const auto op0_type = TM->CGetTreeReindex(tree_helper::CGetType(op0)->index);
                                 const auto op0_const_node = TM->CreateUniqueIntegerCst(static_cast<long long int>(trailing_zero_op0), GET_INDEX_CONST_NODE(op0_type));
                                 const auto op0_expr = IRman->create_binary_operation(op0_type, me->op0, op0_const_node, srcp_default, rshift_expr_K);
                                 const auto op0_ga = IRman->CreateGimpleAssign(op0_type, nullptr, nullptr, op0_expr, function_id, B_id, srcp_default);
                                 INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "Created " + STR(op0_ga));
                                 B->PushBefore(op0_ga, stmt, AppM);
                                 const auto op0_ga_var = GetPointer<const gimple_assign>(GET_CONST_NODE(op0_ga))->op0;
                                 TM->ReplaceTreeNode(stmt, me->op0, op0_ga_var);
                                 /// set the bit_values to the ssa var
                                 if(GetPointer<const ssa_name>(op0))
                                 {
                                    auto op0_ssa = GetPointer<ssa_name>(GET_NODE(op0_ga_var));
                                    op0_ssa->bit_values = GetPointer<const ssa_name>(op0)->bit_values.substr(0, GetPointer<const ssa_name>(op0)->bit_values.size() - trailing_zero_op0);
                                    constrainSSA(op0_ssa, TM);
                                 }
                              }
                              if(trailing_zero_op1 != 0 and op0->index != op1->index)
                              {
                                 const auto op1_type = TM->CGetTreeReindex(tree_helper::CGetType(op1)->index);
                                 const auto op1_const_node = TM->CreateUniqueIntegerCst(static_cast<long long int>(trailing_zero_op1), GET_INDEX_CONST_NODE(op1_type));
                                 const auto op1_expr = IRman->create_binary_operation(op1_type, me->op1, op1_const_node, srcp_default, rshift_expr_K);
                                 const auto op1_ga = IRman->CreateGimpleAssign(op1_type, tree_nodeRef(), tree_nodeRef(), op1_expr, function_id, B_id, srcp_default);
                                 INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "Created " + STR(op1_ga));
                                 B->PushBefore(op1_ga, stmt, AppM);
                                 const auto op1_ga_var = GetPointer<const gimple_assign>(GET_CONST_NODE(op1_ga))->op0;
                                 TM->ReplaceTreeNode(stmt, me->op1, op1_ga_var);
                                 /// set the bit_values to the ssa var
                                 if(GetPointer<const ssa_name>(op1))
                                 {
                                    auto* op1_ssa = GetPointer<ssa_name>(GET_NODE(op1_ga_var));
                                    op1_ssa->bit_values = GetPointer<const ssa_name>(op1)->bit_values.substr(0, GetPointer<const ssa_name>(op1)->bit_values.size() - trailing_zero_op1);
                                    constrainSSA(op1_ssa, TM);
                                 }
                              }

                              const auto ssa_vd = IRman->create_ssa_name(nullptr, ga_op_type, nullptr, nullptr);
                              auto sn = GetPointer<ssa_name>(GET_NODE(ssa_vd));
                              /// set the bit_values to the ssa var
                              sn->bit_values = ssa->bit_values.substr(0, ssa->bit_values.size() - trailing_zero_op0 - trailing_zero_op1);
                              constrainSSA(sn, TM);
                              const auto op_const_node = TM->CreateUniqueIntegerCst((trailing_zero_op0 + trailing_zero_op1), GET_INDEX_CONST_NODE(ga_op_type));
                              const auto op_expr = IRman->create_binary_operation(ga_op_type, ssa_vd, op_const_node, srcp_default, lshift_expr_K);
                              const auto curr_ga = IRman->CreateGimpleAssign(ga_op_type, ssa->min, ssa->max, op_expr, function_id, B_id, srcp_default);
                              INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "Created " + STR(curr_ga));
                              TM->ReplaceTreeNode(curr_ga, GetPointer<const gimple_assign>(GET_CONST_NODE(curr_ga))->op0, ga->op0);
                              TM->ReplaceTreeNode(stmt, ga->op0, ssa_vd);
                              B->PushAfter(curr_ga, stmt, AppM);
                              INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "pushed");
                           }
                        }
                     };
                     mult_expr_BVO();
                  }
                  else if(GET_CONST_NODE(ga->op1)->get_kind() == plus_expr_K || GET_CONST_NODE(ga->op1)->get_kind() == minus_expr_K)
                  {
                     auto plus_minus_BVO = [&] {
                        const auto me = GetPointer<const binary_expr>(GET_CONST_NODE(ga->op1));
                        if(me->op0->index == me->op1->index)
                        {
                           if(GET_CONST_NODE(ga->op1)->get_kind() == minus_expr_K)
                           {
                              TM->ReplaceTreeNode(stmt, ga->op1, TM->CreateUniqueIntegerCst(0LL, GET_INDEX_CONST_NODE(ga_op_type)));
                              INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---Statement transformed in " + GET_NODE(stmt)->ToString());
                              modified = true;
                              AppM->RegisterTransformation(GetName(), stmt);
                           }
                           else
                           {
                              const auto op_const_node = TM->CreateUniqueIntegerCst(static_cast<long long int>(1), GET_INDEX_CONST_NODE(ga_op_type));
                              const auto srcp_default = ga->include_name + ":" + STR(ga->line_number) + ":" + STR(ga->column_number);
                              const auto op_expr = IRman->create_binary_operation(ga_op_type, me->op0, op_const_node, srcp_default, lshift_expr_K);
                              TM->ReplaceTreeNode(stmt, ga->op1, op_expr);
                              INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---Statement transformed in " + GET_NODE(stmt)->ToString());
                              modified = true;
                              AppM->RegisterTransformation(GetName(), stmt);
                           }
                           return;
                        }
                        const auto op0 = GET_CONST_NODE(me->op0);
                        const auto op1 = GET_CONST_NODE(me->op1);
                        PRINT_DBG_MEX(DEBUG_LEVEL_PEDANTIC, debug_level, "Var_uid: " + AppM->CGetFunctionBehavior(function_id)->CGetBehavioralHelper()->PrintVariable(output_uid) + " bitstring: " + bit_values);
                        unsigned int trailing_zero_op0 = 0;
                        unsigned int trailing_zero_op1 = 0;
                        bool is_op0_null = false;
                        bool is_op1_null = false;

                        if(GetPointer<const ssa_name>(op0) && GET_CONST_NODE(ga->op1)->get_kind() == plus_expr_K)
                        {
                           const auto& bit_values_op0 = GetPointer<const ssa_name>(op0)->bit_values;
                           PRINT_DBG_MEX(DEBUG_LEVEL_PEDANTIC, debug_level, "Var_uid: " + AppM->CGetFunctionBehavior(function_id)->CGetBehavioralHelper()->PrintVariable(GET_INDEX_CONST_NODE(me->op0)) + " bitstring: " + bit_values_op0);
                           for(const auto& current_el : boost::adaptors::reverse(bit_values_op0))
                           {
                              if(current_el == '0' || current_el == 'X')
                              {
                                 ++trailing_zero_op0;
                              }
                              else
                              {
                                 break;
                              }
                           }
                           if(bit_values_op0 == "0")
                           {
                              is_op0_null = true;
                           }
                        }
                        else if(GetPointer<const integer_cst>(op0) && GET_CONST_NODE(ga->op1)->get_kind() == plus_expr_K)
                        {
                           const auto int_const = GetPointer<const integer_cst>(op0);
                           const auto value_int = static_cast<unsigned long long int>(int_const->value);
                           for(unsigned int index = 0; index < 64 && value_int != 0; ++index)
                           {
                              if(value_int & (1ULL << index))
                              {
                                 break;
                              }
                              else
                              {
                                 ++trailing_zero_op0;
                              }
                           }
                           if(int_const->value == 0)
                           {
                              is_op0_null = true;
                           }
                        }

                        if(GetPointer<const ssa_name>(op1))
                        {
                           const auto& bit_values_op1 = GetPointer<const ssa_name>(op1)->bit_values;
                           PRINT_DBG_MEX(DEBUG_LEVEL_PEDANTIC, debug_level, "Var_uid: " + AppM->CGetFunctionBehavior(function_id)->CGetBehavioralHelper()->PrintVariable(GET_INDEX_CONST_NODE(me->op1)) + " bitstring: " + bit_values_op1);
                           for(const auto& current_el : boost::adaptors::reverse(bit_values_op1))
                           {
                              if(current_el == '0' || current_el == 'X')
                              {
                                 ++trailing_zero_op1;
                              }
                              else
                              {
                                 break;
                              }
                           }
                           if(bit_values_op1 == "0")
                           {
                              is_op1_null = true;
                           }
                        }
                        else if(GetPointer<const integer_cst>(op1))
                        {
                           const auto int_const = GetPointer<const integer_cst>(op1);
                           const auto value_int = static_cast<unsigned long long int>(int_const->value);
                           for(unsigned int index = 0; index < 64 && value_int != 0; ++index)
                           {
                              if(value_int & (1ULL << index))
                              {
                                 break;
                              }
                              else
                              {
                                 ++trailing_zero_op1;
                              }
                           }
                           if(int_const->value == 0)
                           {
                              is_op1_null = true;
                           }
                        }

                        INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---Trailing zeros op0=" + STR(trailing_zero_op0) + ", trailing zeros op1=" + STR(trailing_zero_op1));
                        if(is_op0_null)
                        {
                           propagateValue(ssa, TM, ga->op0, me->op1);
                        }
                        else if(is_op1_null)
                        {
                           propagateValue(ssa, TM, ga->op0, me->op0);
                        }
                        else if(trailing_zero_op0 != 0 || trailing_zero_op1 != 0)
                        {
                           modified = true;
                           AppM->RegisterTransformation(GetName(), stmt);
                           const auto srcp_default = ga->include_name + ":" + STR(ga->line_number) + ":" + STR(ga->column_number);
                           const auto is_first_max = trailing_zero_op0 > trailing_zero_op1;
                           const auto shift_const = is_first_max ? trailing_zero_op0 : trailing_zero_op1;
                           INDENT_OUT_MEX(OUTPUT_LEVEL_VERBOSE, output_level, "---Bit Value Opt: " + (GET_CONST_NODE(ga->op1)->get_kind() == plus_expr_K ? std::string("plus_expr") : std::string("minus_expr")) + " optimized, nbits = " + STR(shift_const));
                           const auto shift_constant_node = TM->CreateUniqueIntegerCst(static_cast<long long int>(shift_const), GET_INDEX_CONST_NODE(ga_op_type));
                           const auto is_op0_const = tree_helper::is_constant(TM, op0->index);
                           const auto is_op1_const = tree_helper::is_constant(TM, op1->index);
                           const auto op0_type = TM->CGetTreeReindex(tree_helper::CGetType(op0)->index);
                           const auto op1_type = TM->CGetTreeReindex(tree_helper::CGetType(op1)->index);
                           const auto b_node = is_first_max ? me->op1 : me->op0;
                           const auto b_type = is_first_max ? op1_type : op0_type;

                           if(is_op0_const)
                           {
                              const auto int_const = GetPointer<const integer_cst>(op0);
                              if(tree_helper::is_int(TM, op0->index))
                              {
                                 if(static_cast<long long int>(int_const->value >> shift_const) == 0)
                                 {
                                    is_op0_null = GET_CONST_NODE(ga->op1)->get_kind() == plus_expr_K; // TODO: true?
                                 }
                                 TM->ReplaceTreeNode(stmt, me->op0, TM->CreateUniqueIntegerCst(static_cast<long long int>(int_const->value >> shift_const), GET_INDEX_CONST_NODE(op0_type)));
                              }
                              else
                              {
                                 if(static_cast<unsigned long long int>(int_const->value >> shift_const) == 0)
                                 {
                                    is_op0_null = GET_CONST_NODE(ga->op1)->get_kind() == plus_expr_K; // TODO: true?
                                 }
                                 TM->ReplaceTreeNode(stmt, me->op0, TM->CreateUniqueIntegerCst(static_cast<long long int>(static_cast<unsigned long long int>(int_const->value) >> shift_const), GET_INDEX_CONST_NODE(op0_type)));
                              }
                           }
                           else
                           {
                              std::string resulting_bit_values;
                              THROW_ASSERT(GetPointer<const ssa_name>(op0), "expected an SSA name");

                              if((GetPointer<const ssa_name>(op0)->bit_values.size() - shift_const) > 0)
                              {
                                 resulting_bit_values = GetPointer<const ssa_name>(op0)->bit_values.substr(0, GetPointer<const ssa_name>(op0)->bit_values.size() - shift_const);
                              }
                              else if(tree_helper::is_int(TM, op0->index))
                              {
                                 resulting_bit_values = GetPointer<const ssa_name>(op0)->bit_values.substr(0, 1);
                              }
                              else
                              {
                                 resulting_bit_values = "0";
                              }

                              if(resulting_bit_values == "0" && GET_CONST_NODE(ga->op1)->get_kind() == plus_expr_K)
                              {
                                 is_op0_null = true;
                              }
                              else
                              {
                                 const auto op0_expr = IRman->create_binary_operation(op0_type, me->op0, shift_constant_node, srcp_default, rshift_expr_K);
                                 const auto op0_ga = IRman->CreateGimpleAssign(op0_type, nullptr, nullptr, op0_expr, function_id, B_id, srcp_default);
                                 INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "Created " + STR(op0_ga));
                                 B->PushBefore(op0_ga, stmt, AppM);
                                 const auto op0_ga_var = GetPointer<const gimple_assign>(GET_CONST_NODE(op0_ga))->op0;
                                 INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---Replacing " + me->op0->ToString() + " with " + op0_ga_var->ToString() + " in " + stmt->ToString());
                                 TM->ReplaceTreeNode(stmt, me->op0, op0_ga_var);
#if HAVE_FROM_DISCREPANCY_BUILT
                                 /*
                                  * for discrepancy analysis, the ssa assigned by this
                                  * bitshift must not be checked if it was applied to
                                  * a variable marked as address.
                                  */
                                 if(parameters->isOption(OPT_discrepancy) and parameters->getOption<bool>(OPT_discrepancy))
                                 {
                                    AppM->RDiscr->ssa_to_skip_if_address.insert(GET_NODE(op0_ga_var));
                                 }
#endif
                                 /// set the bit_values to the ssa var
                                 auto op0_ssa = GetPointer<ssa_name>(GET_NODE(op0_ga_var));
                                 op0_ssa->bit_values = resulting_bit_values;
                                 constrainSSA(op0_ssa, TM);
                                 PRINT_DBG_MEX(DEBUG_LEVEL_PEDANTIC, debug_level, "Var_uid: " + AppM->CGetFunctionBehavior(function_id)->CGetBehavioralHelper()->PrintVariable(GET_INDEX_CONST_NODE(op0_ga_var)) + " bitstring: " + STR(op0_ssa->bit_values));
                              }
                           }

                           if(is_op1_const)
                           {
                              const auto int_const = GetPointer<const integer_cst>(op1);
                              if(tree_helper::is_int(TM, op1->index))
                              {
                                 if(static_cast<long long int>(int_const->value >> shift_const) == 0)
                                 {
                                    is_op1_null = true;
                                 }
                                 TM->ReplaceTreeNode(stmt, me->op1, TM->CreateUniqueIntegerCst(static_cast<long long int>(int_const->value >> shift_const), GET_INDEX_CONST_NODE(op1_type)));
                              }
                              else
                              {
                                 if(static_cast<unsigned long long int>(int_const->value >> shift_const) == 0)
                                 {
                                    is_op1_null = true;
                                 }
                                 TM->ReplaceTreeNode(stmt, me->op1, TM->CreateUniqueIntegerCst(static_cast<long long int>(static_cast<unsigned long long int>(int_const->value) >> shift_const), GET_INDEX_CONST_NODE(op1_type)));
                              }
                           }
                           else
                           {
                              std::string resulting_bit_values;
                              THROW_ASSERT(GetPointer<const ssa_name>(op1), "expected an SSA name");

                              if((GetPointer<const ssa_name>(op1)->bit_values.size() - shift_const) > 0)
                              {
                                 resulting_bit_values = GetPointer<const ssa_name>(op1)->bit_values.substr(0, GetPointer<const ssa_name>(op1)->bit_values.size() - shift_const);
                              }
                              else if(tree_helper::is_int(TM, op1->index))
                              {
                                 resulting_bit_values = GetPointer<const ssa_name>(op1)->bit_values.substr(0, 1);
                              }
                              else
                              {
                                 resulting_bit_values = "0";
                              }

                              if(resulting_bit_values == "0")
                              {
                                 is_op1_null = true;
                              }
                              else
                              {
                                 const auto op1_expr = IRman->create_binary_operation(op1_type, me->op1, shift_constant_node, srcp_default, rshift_expr_K);
                                 const auto op1_ga = IRman->CreateGimpleAssign(op1_type, nullptr, nullptr, op1_expr, function_id, B_id, srcp_default);
                                 INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "Created " + STR(op1_ga));
                                 B->PushBefore(op1_ga, stmt, AppM);
                                 const auto op1_ga_var = GetPointer<const gimple_assign>(GET_CONST_NODE(op1_ga))->op0;
                                 INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---Replacing " + me->op1->ToString() + " with " + op1_ga_var->ToString() + " in " + stmt->ToString());
                                 TM->ReplaceTreeNode(stmt, me->op1, op1_ga_var);
#if HAVE_FROM_DISCREPANCY_BUILT
                                 /*
                                  * for discrepancy analysis, the ssa assigned by this
                                  * bitshift must not be checked if it was applied to
                                  * a variable marked as address.
                                  */
                                 if(parameters->isOption(OPT_discrepancy) and parameters->getOption<bool>(OPT_discrepancy))
                                 {
                                    AppM->RDiscr->ssa_to_skip_if_address.insert(GET_NODE(op1_ga_var));
                                 }
#endif
                                 /// set the bit_values to the ssa var
                                 auto* op1_ssa = GetPointer<ssa_name>(GET_NODE(op1_ga_var));
                                 op1_ssa->bit_values = resulting_bit_values;
                                 constrainSSA(op1_ssa, TM);
                                 PRINT_DBG_MEX(DEBUG_LEVEL_PEDANTIC, debug_level, "Var_uid: " + AppM->CGetFunctionBehavior(function_id)->CGetBehavioralHelper()->PrintVariable(GET_INDEX_CONST_NODE(op1_ga_var)) + " bitstring: " + STR(op1_ssa->bit_values));
                              }
                           }

                           tree_nodeRef curr_ga;
                           if(is_op0_null)
                           {
                              curr_ga = IRman->CreateGimpleAssign(op1_type, nullptr, nullptr, me->op1, function_id, B_id, srcp_default);
                              INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "Created " + STR(curr_ga));
                           }
                           else if(is_op1_null)
                           {
                              curr_ga = IRman->CreateGimpleAssign(op1_type, nullptr, nullptr, me->op0, function_id, B_id, srcp_default);
                              INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "Created " + STR(curr_ga));
                           }
                           else
                           {
                              curr_ga = IRman->CreateGimpleAssign(op1_type, nullptr, nullptr, ga->op1, function_id, B_id, srcp_default);
                              INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "Created " + STR(curr_ga));
                           }
                           B->PushBefore(curr_ga, stmt, AppM);
                           const auto curr_ga_var = GetPointer<const gimple_assign>(GET_CONST_NODE(curr_ga))->op0;
#if HAVE_FROM_DISCREPANCY_BUILT
                           /*
                            * for discrepancy analysis, the ssa assigned by this
                            * bitshift must not be checked if it was applied to
                            * a variable marked as address.
                            */
                           if(parameters->isOption(OPT_discrepancy) and parameters->getOption<bool>(OPT_discrepancy))
                           {
                              AppM->RDiscr->ssa_to_skip_if_address.insert(GET_NODE(curr_ga_var));
                           }
#endif
                           /// set the bit_values to the ssa var
                           auto op_ssa = GetPointer<ssa_name>(GET_NODE(curr_ga_var));
                           op_ssa->bit_values = ssa->bit_values.substr(0, ssa->bit_values.size() - shift_const);
                           constrainSSA(op_ssa, TM);
                           PRINT_DBG_MEX(DEBUG_LEVEL_PEDANTIC, debug_level, "Var_uid: " + AppM->CGetFunctionBehavior(function_id)->CGetBehavioralHelper()->PrintVariable(GET_INDEX_CONST_NODE(curr_ga_var)) + " bitstring: " + STR(op_ssa->bit_values));

                           const auto op_expr = IRman->create_binary_operation(ga_op_type, curr_ga_var, shift_constant_node, srcp_default, lshift_expr_K);
                           const auto lshift_ga = IRman->CreateGimpleAssign(ga_op_type, nullptr, nullptr, op_expr, function_id, B_id, srcp_default);
                           INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "Created " + STR(lshift_ga));
                           B->PushBefore(lshift_ga, stmt, AppM);
                           const auto lshift_ga_var = GetPointer<const gimple_assign>(GET_CONST_NODE(lshift_ga))->op0;
                           /// set the bit_values to the ssa var
                           auto lshift_ssa = GetPointer<ssa_name>(GET_NODE(lshift_ga_var));
                           lshift_ssa->bit_values = ssa->bit_values.substr(0, ssa->bit_values.size() - shift_const);
                           while(lshift_ssa->bit_values.size() < ssa->bit_values.size())
                           {
                              lshift_ssa->bit_values.push_back('0');
                           }
                           constrainSSA(lshift_ssa, TM);
                           PRINT_DBG_MEX(DEBUG_LEVEL_PEDANTIC, debug_level, "Var_uid: " + AppM->CGetFunctionBehavior(function_id)->CGetBehavioralHelper()->PrintVariable(GET_INDEX_CONST_NODE(lshift_ga_var)) + " bitstring: " + STR(lshift_ssa->bit_values));

                           bool do_final_or = false;
                           unsigned int n_iter = 0;
                           for(const auto& cur_bit : boost::adaptors::reverse(ssa->bit_values))
                           {
                              if(cur_bit == '1' || cur_bit == 'U')
                              {
                                 do_final_or = true;
                                 break;
                              }
                              n_iter++;
                              if(n_iter == shift_const)
                              {
                                 break;
                              }
                           }

                           if(do_final_or)
                           {
#if HAVE_FROM_DISCREPANCY_BUILT
                              /*
                               * for discrepancy analysis, the ssa assigned by this
                               * bitshift must not be checked if it was applied to
                               * a variable marked as address.
                               */
                              if(parameters->isOption(OPT_discrepancy) and parameters->getOption<bool>(OPT_discrepancy))
                              {
                                 AppM->RDiscr->ssa_to_skip_if_address.insert(GET_NODE(lshift_ga_var));
                              }
#endif
                              if(GetPointer<const integer_cst>(GET_CONST_NODE(b_node)))
                              {
                                 const auto int_const = GetPointer<const integer_cst>(GET_CONST_NODE(b_node));
                                 const auto b_node_val = TM->CreateUniqueIntegerCst(static_cast<long long int>(static_cast<unsigned long long int>(int_const->value) & ((1ULL << shift_const) - 1)), b_type->index);
                                 TM->ReplaceTreeNode(stmt, ga->op1, IRman->create_ternary_operation(ga_op_type, lshift_ga_var, b_node_val, shift_constant_node, srcp_default, bit_ior_concat_expr_K));
                              }
                              else
                              {
                                 const auto bit_mask_constant_node = TM->CreateUniqueIntegerCst(static_cast<long long int>((1ULL << shift_const) - 1), b_type->index);
                                 const auto band_expr = IRman->create_binary_operation(b_type, b_node, bit_mask_constant_node, srcp_default, bit_and_expr_K);
                                 const auto band_ga = IRman->CreateGimpleAssign(b_type, nullptr, nullptr, band_expr, function_id, B_id, srcp_default);
                                 INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "Created " + STR(band_ga));
                                 B->PushBefore(band_ga, stmt, AppM);
                                 const auto band_ga_var = GetPointer<const gimple_assign>(GET_CONST_NODE(band_ga))->op0;
#if HAVE_FROM_DISCREPANCY_BUILT
                                 /*
                                  * for discrepancy analysis, the ssa assigned by this
                                  * bitshift must not be checked if it was applied to
                                  * a variable marked as address.
                                  */
                                 if(parameters->isOption(OPT_discrepancy) and parameters->getOption<bool>(OPT_discrepancy))
                                 {
                                    AppM->RDiscr->ssa_to_skip_if_address.insert(GET_NODE(band_ga_var));
                                 }
#endif
                                 /// set the bit_values to the ssa var
                                 auto band_ssa = GetPointer<ssa_name>(GET_NODE(band_ga_var));
                                 for(const auto& cur_bit : boost::adaptors::reverse(ssa->bit_values))
                                 {
                                    band_ssa->bit_values = cur_bit + band_ssa->bit_values;
                                    if(band_ssa->bit_values.size() == shift_const)
                                    {
                                       break;
                                    }
                                 }
                                 band_ssa->bit_values = "0" + band_ssa->bit_values;
                                 constrainSSA(band_ssa, TM);
                                 PRINT_DBG_MEX(DEBUG_LEVEL_PEDANTIC, debug_level, "Var_uid: " + AppM->CGetFunctionBehavior(function_id)->CGetBehavioralHelper()->PrintVariable(GET_INDEX_CONST_NODE(band_ga_var)) + " bitstring: " + STR(band_ssa->bit_values));

                                 const auto res_expr = IRman->create_ternary_operation(ga_op_type, lshift_ga_var, band_ga_var, shift_constant_node, srcp_default, bit_ior_concat_expr_K);
                                 TM->ReplaceTreeNode(stmt, ga->op1, res_expr);
                              }
                           }
                           else
                           {
                              PRINT_DBG_MEX(DEBUG_LEVEL_PEDANTIC, debug_level, "Final or not performed: ");
                              TM->ReplaceTreeNode(stmt, ga->op1, lshift_ga_var);
                           }
                           /// set uses of stmt
                        }
                        else if(GET_CONST_NODE(ga->op1)->get_kind() == minus_expr_K && GetPointer<const integer_cst>(op0) && GetPointer<const integer_cst>(op0)->value == 0)
                        {
                           if(!parameters->isOption(OPT_use_ALUs) || !parameters->getOption<bool>(OPT_use_ALUs))
                           {
                              const auto srcp_default = ga->include_name + ":" + STR(ga->line_number) + ":" + STR(ga->column_number);
                              const auto res_expr = IRman->create_unary_operation(ga_op_type, me->op1, srcp_default, negate_expr_K);
                              TM->ReplaceTreeNode(stmt, ga->op1, res_expr);
                              modified = true;
                              AppM->RegisterTransformation(GetName(), stmt);
                           }
                        }
                     };
                     plus_minus_BVO();
                  }
                  else if(GET_CONST_NODE(ga->op1)->get_kind() == eq_expr_K || GET_CONST_NODE(ga->op1)->get_kind() == ne_expr_K)
                  {
                     auto eq_ne_expr_BVO = [&] {
                        const auto me = GetPointer<const binary_expr>(GET_CONST_NODE(ga->op1));
                        const auto op0 = GET_CONST_NODE(me->op0);
                        const auto op1 = GET_CONST_NODE(me->op1);
                        if(tree_helper::CGetType(op0)->get_kind() == real_type_K && tree_helper::CGetType(op1)->get_kind() == real_type_K)
                        {
                           // TODO: adapt existing operations to real type (zero sign bug to be considered)
                           return;
                        }
                        const auto op0_size = BitLatticeManipulator::size(TM, op0->index);
                        bool is_op1_zero = false;
                        if(GetPointer<const integer_cst>(op1))
                        {
                           const auto ic = GetPointer<integer_cst>(op1);
                           const auto ull_value = static_cast<unsigned long long int>(tree_helper::get_integer_cst_value(ic));
                           if(ull_value == 0)
                           {
                              is_op1_zero = true;
                           }
                        }

                        if(op0 == op1)
                        {
                           const auto const_value = GET_CONST_NODE(ga->op1)->get_kind() == eq_expr_K ? 1LL : 0LL;
                           const auto val = TM->CreateUniqueIntegerCst(const_value, GET_INDEX_CONST_NODE(ga_op_type));
                           propagateValue(ssa, TM, ga->op0, val);
                        }
                        else if(is_op1_zero && GET_CONST_NODE(ga->op1)->get_kind() == ne_expr_K && op0_size == 1)
                        {
                           const auto op0_op_type = TM->CGetTreeReindex(tree_helper::CGetType(op0)->index);
                           const auto data_bitsize = BitLatticeManipulator::size(TM, GET_INDEX_CONST_NODE(op0_op_type));
                           if(data_bitsize == 1)
                           {
                              propagateValue(ssa, TM, ga->op0, me->op0);
                              if(!ssa->CGetUseStmts().empty())
                              {
                                 if(AppM->ApplyNewTransformation())
                                 {
                                    const auto srcp_default = ga->include_name + ":" + STR(ga->line_number) + ":" + STR(ga->column_number);
                                    const auto nop_expr_node = IRman->create_unary_operation(ga_op_type, me->op0, srcp_default, nop_expr_K);
                                    TM->ReplaceTreeNode(stmt, ga->op1, nop_expr_node);
                                    modified = true;
                                    AppM->RegisterTransformation(GetName(), stmt);
                                 }
                              }
                           }
                           else
                           {
                              const auto srcp_default = ga->include_name + ":" + STR(ga->line_number) + ":" + STR(ga->column_number);
                              const auto one_const_node = TM->CreateUniqueIntegerCst(1, GET_INDEX_CONST_NODE(op0_op_type));
                              const auto bitwise_masked = IRman->create_binary_operation(op0_op_type, me->op0, one_const_node, srcp_default, bit_and_expr_K);
                              const auto op0_ga =
                                  IRman->CreateGimpleAssign(op0_op_type, TM->CreateUniqueIntegerCst(0, GET_INDEX_CONST_NODE(op0_op_type)), TM->CreateUniqueIntegerCst(1LL, GET_INDEX_CONST_NODE(op0_op_type)), bitwise_masked, function_id, B_id, srcp_default);
                              INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "Created " + STR(op0_ga));
                              B->PushBefore(op0_ga, stmt, AppM);
                              const auto op0_ga_var = GetPointer<const gimple_assign>(GET_CONST_NODE(op0_ga))->op0;

                              const auto ga_nop = IRman->CreateNopExpr(op0_ga_var, ga_op_type, tree_nodeRef(), tree_nodeRef(), function_id);
                              B->PushBefore(ga_nop, stmt, AppM);
                              modified = true;
                              AppM->RegisterTransformation(GetName(), ga_nop);
                              const auto nop_ga_var = GetPointer<const gimple_assign>(GET_CONST_NODE(ga_nop))->op0;
                              TM->ReplaceTreeNode(stmt, ga->op1, nop_ga_var);
                           }
                        }
                        else
                        {
                           rel_expr_BVO();
                        }
                     };
                     eq_ne_expr_BVO();
                  }
                  else if(GET_CONST_NODE(ga->op1)->get_kind() == lt_expr_K || GET_CONST_NODE(ga->op1)->get_kind() == gt_expr_K || GET_CONST_NODE(ga->op1)->get_kind() == le_expr_K || GET_CONST_NODE(ga->op1)->get_kind() == ge_expr_K)
                  {
                     rel_expr_BVO();
                  }
                  else if(GET_CONST_NODE(ga->op1)->get_kind() == bit_and_expr_K || GET_CONST_NODE(ga->op1)->get_kind() == bit_xor_expr_K)
                  {
                     auto bit_expr_BVO = [&] {
                        const auto me = GetPointer<const binary_expr>(GET_CONST_NODE(ga->op1));
                        const auto op0 = GET_CONST_NODE(me->op0);
                        const auto op1 = GET_CONST_NODE(me->op1);
                        const auto expr_kind = GET_CONST_NODE(ga->op1)->get_kind();

                        std::string s0, s1;
                        if(GetPointer<const ssa_name>(op0))
                        {
                           s0 = GetPointer<const ssa_name>(op0)->bit_values;
                        }
                        if(GetPointer<const ssa_name>(op1))
                        {
                           s1 = GetPointer<const ssa_name>(op1)->bit_values;
                        }
                        unsigned int precision;
                        if(s0.size() && s1.size())
                        {
                           precision = static_cast<unsigned int>(std::min(s0.size(), s1.size()));
                        }
                        else
                        {
                           precision = static_cast<unsigned int>(std::max(s0.size(), s1.size()));
                        }

                        if(precision)
                        {
                           unsigned int trailing_zero = 0;
                           bool is_zero0 = s0 == "0";
                           if(GetPointer<const integer_cst>(op0))
                           {
                              const auto ic = GetPointer<const integer_cst>(op0);
                              const auto ull_value = static_cast<unsigned long long int>(tree_helper::get_integer_cst_value(ic));
                              is_zero0 = ull_value == 0;
                              s0 = convert_to_binary(ull_value, precision);
                           }
                           bool is_zero1 = s1 == "0";
                           if(GetPointer<integer_cst>(op1))
                           {
                              const auto ic = GetPointer<const integer_cst>(op1);
                              const auto ull_value = static_cast<unsigned long long int>(tree_helper::get_integer_cst_value(ic));
                              is_zero1 = ull_value == 0;
                              s1 = convert_to_binary(ull_value, precision);
                           }
                           if(is_zero0 || is_zero1)
                           {
                              if(GET_CONST_NODE(ga->op1)->get_kind() == bit_and_expr_K)
                              {
                                 INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace bit_and_expr usage before: " + stmt->ToString());
                                 const auto zero_node = TM->CreateUniqueIntegerCst(0LL, tree_helper::CGetType(op0)->index);
                                 propagateValue(ssa, TM, ga->op0, zero_node);
                              }
                              else
                              {
                                 // bit_xor_expr_K
                                 INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace bit_xor_expr usage before: " + stmt->ToString());
                                 const auto val = is_zero0 ? me->op1 : me->op0;
                                 propagateValue(ssa, TM, ga->op0, val);
                              }
                           }
                           else
                           {
                              for(auto s0it = s0.rbegin(), s1it = s1.rbegin(), s0end = s0.rend(), s1end = s1.rend(); s0it != s0end && s1it != s1end; ++s0it, ++s1it)
                              {
                                 if((expr_kind == bit_and_expr_K && (*s0it == '0' || *s1it == '0')) || *s0it == 'X' || *s1it == 'X')
                                 {
                                    ++trailing_zero;
                                 }
                                 else
                                 {
                                    break;
                                 }
                              }
                              if(trailing_zero)
                              {
                                 INDENT_OUT_MEX(OUTPUT_LEVEL_VERBOSE, output_level, "---Bit Value Opt: " + std::string(GET_NODE(ga->op1)->get_kind_text()) + " optimized, nbits = " + STR(trailing_zero));
                                 modified = true;
                                 AppM->RegisterTransformation(GetName(), stmt);
                                 const auto srcp_default = ga->include_name + ":" + STR(ga->line_number) + ":" + STR(ga->column_number);
                                 const auto op0_op_type = TM->CGetTreeReindex(tree_helper::CGetType(op0)->index);
                                 const auto op1_op_type = TM->CGetTreeReindex(tree_helper::CGetType(op1)->index);

                                 if(GetPointer<const ssa_name>(op0))
                                 {
                                    const auto op0_const_node = TM->CreateUniqueIntegerCst(static_cast<long long int>(trailing_zero), GET_INDEX_CONST_NODE(op0_op_type));
                                    const auto op0_expr = IRman->create_binary_operation(op0_op_type, me->op0, op0_const_node, srcp_default, rshift_expr_K);
                                    const auto op0_ga = IRman->CreateGimpleAssign(op0_op_type, nullptr, nullptr, op0_expr, function_id, B_id, srcp_default);
                                    INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "Created " + STR(op0_ga));
                                    B->PushBefore(op0_ga, stmt, AppM);
                                    const auto op0_ga_var = GetPointer<const gimple_assign>(GET_CONST_NODE(op0_ga))->op0;
                                    TM->ReplaceTreeNode(stmt, me->op0, op0_ga_var);
                                    /// set the bit_values to the ssa var
                                    auto op0_ssa = GetPointer<ssa_name>(GET_NODE(op0_ga_var));
                                    op0_ssa->bit_values = GetPointer<const ssa_name>(op0)->bit_values.substr(0, GetPointer<const ssa_name>(op0)->bit_values.size() - trailing_zero);
                                    constrainSSA(op0_ssa, TM);
                                 }
                                 else
                                 {
                                    const auto int_const = GetPointer<const integer_cst>(op0);
                                    if(tree_helper::is_int(TM, op0->index))
                                    {
                                       TM->ReplaceTreeNode(stmt, me->op0, TM->CreateUniqueIntegerCst(static_cast<long long int>(int_const->value >> trailing_zero), GET_INDEX_CONST_NODE(op0_op_type)));
                                    }
                                    else
                                    {
                                       TM->ReplaceTreeNode(stmt, me->op0, TM->CreateUniqueIntegerCst(static_cast<long long int>(static_cast<unsigned long long int>(int_const->value) >> trailing_zero), GET_INDEX_CONST_NODE(op0_op_type)));
                                    }
                                 }
                                 if(GetPointer<const ssa_name>(op1))
                                 {
                                    const auto op1_const_node = TM->CreateUniqueIntegerCst(static_cast<long long int>(trailing_zero), GET_INDEX_CONST_NODE(op1_op_type));
                                    const auto op1_expr = IRman->create_binary_operation(op1_op_type, me->op1, op1_const_node, srcp_default, rshift_expr_K);
                                    const auto op1_ga = IRman->CreateGimpleAssign(op1_op_type, nullptr, nullptr, op1_expr, function_id, B_id, srcp_default);
                                    INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "Created " + STR(op1_ga));
                                    B->PushBefore(op1_ga, stmt, AppM);
                                    const auto op1_ga_var = GetPointer<const gimple_assign>(GET_CONST_NODE(op1_ga))->op0;
                                    TM->ReplaceTreeNode(stmt, me->op1, op1_ga_var);
                                    /// set the bit_values to the ssa var
                                    auto op1_ssa = GetPointer<ssa_name>(GET_NODE(op1_ga_var));
                                    op1_ssa->bit_values = GetPointer<const ssa_name>(op1)->bit_values.substr(0, GetPointer<const ssa_name>(op1)->bit_values.size() - trailing_zero);
                                    constrainSSA(op1_ssa, TM);
                                 }
                                 else
                                 {
                                    const auto int_const = GetPointer<const integer_cst>(op1);
                                    if(tree_helper::is_int(TM, op1->index))
                                    {
                                       TM->ReplaceTreeNode(stmt, me->op1, TM->CreateUniqueIntegerCst(static_cast<long long int>(int_const->value >> trailing_zero), GET_INDEX_CONST_NODE(op1_op_type)));
                                    }
                                    else
                                    {
                                       TM->ReplaceTreeNode(stmt, me->op1, TM->CreateUniqueIntegerCst(static_cast<long long int>(static_cast<unsigned long long int>(int_const->value) >> trailing_zero), GET_INDEX_CONST_NODE(op1_op_type)));
                                    }
                                 }

                                 const auto ssa_vd = IRman->create_ssa_name(nullptr, ga_op_type, nullptr, nullptr);
                                 auto sn = GetPointer<ssa_name>(GET_NODE(ssa_vd));
                                 /// set the bit_values to the ssa var
                                 sn->bit_values = ssa->bit_values.substr(0, ssa->bit_values.size() - trailing_zero);
                                 constrainSSA(sn, TM);
                                 const auto op_const_node = TM->CreateUniqueIntegerCst(static_cast<long long int>(trailing_zero), GET_INDEX_CONST_NODE(ga_op_type));
                                 const auto op_expr = IRman->create_binary_operation(ga_op_type, ssa_vd, op_const_node, srcp_default, lshift_expr_K);
                                 const auto curr_ga = IRman->CreateGimpleAssign(ga_op_type, ssa->min, ssa->max, op_expr, function_id, B_id, srcp_default);
                                 INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "Created " + STR(curr_ga));
                                 TM->ReplaceTreeNode(curr_ga, GetPointer<const gimple_assign>(GET_CONST_NODE(curr_ga))->op0, ga->op0);
                                 TM->ReplaceTreeNode(stmt, ga->op0, ssa_vd);
                                 B->PushAfter(curr_ga, stmt, AppM);
                                 INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "pushed");
                              }
                           }
                        }
                     };
                     bit_expr_BVO();
                  }
                  else if(GET_CONST_NODE(ga->op1)->get_kind() == cond_expr_K)
                  {
                     auto cond_expr_BVO = [&] {
                        const auto me = GetPointer<const cond_expr>(GET_CONST_NODE(ga->op1));
                        if(!tree_helper::is_bool(TM, GET_INDEX_CONST_NODE(me->op0)))
                        {
                           /// try to fix cond_expr condition
                           const auto cond_op0_ssa = GetPointer<const ssa_name>(GET_CONST_NODE(me->op0));
                           if(cond_op0_ssa)
                           {
                              const auto defStmt = GET_CONST_NODE(cond_op0_ssa->CGetDefStmt());
                              if(defStmt->get_kind() == gimple_assign_K)
                              {
                                 const auto prev_ga = GetPointer<const gimple_assign>(defStmt);
                                 const auto prev_code1 = GET_CONST_NODE(prev_ga->op1)->get_kind();
                                 if(prev_code1 == nop_expr_K)
                                 {
                                    const auto ne = GetPointer<const nop_expr>(GET_CONST_NODE(prev_ga->op1));
                                    if(tree_helper::is_bool(TM, GET_INDEX_CONST_NODE(ne->op)))
                                    {
                                       INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace cond_expr condition before: " + stmt->ToString());
                                       TM->ReplaceTreeNode(stmt, me->op0, ne->op);
                                       INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace cond_expr condition after: " + stmt->ToString());
                                       modified = true;
                                       AppM->RegisterTransformation(GetName(), stmt);
                                    }
                                 }
                              }
                           }
                        }
                        if(!AppM->ApplyNewTransformation())
                        {
                           INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "<--Skipped because reached limit of cfg transformations");
                           return;
                        }
                        const auto op0 = GET_CONST_NODE(me->op1);
                        const auto op1 = GET_CONST_NODE(me->op2);
                        const auto condition = GET_CONST_NODE(me->op0);
                        if(op0 == op1)
                        {
                           INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---Cond expr with equal operands");
                           propagateValue(ssa, TM, ga->op0, me->op1);
                        }
                        else if(condition->get_kind() == integer_cst_K)
                        {
                           INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---Cond expr with constant condition");
                           const auto ic = GetPointer<const integer_cst>(condition);
                           const auto ull_value = static_cast<unsigned long long int>(tree_helper::get_integer_cst_value(ic));
                           const auto val = ull_value ? me->op1 : me->op2;
                           propagateValue(ssa, TM, ga->op0, val);
                        }
                        else
                        {
                           THROW_ASSERT(op0 != op1, "unexpected condition");
                           std::string s0, s1;
                           if(GetPointer<const ssa_name>(op0))
                           {
                              s0 = GetPointer<const ssa_name>(op0)->bit_values;
                           }
                           if(GetPointer<const ssa_name>(op1))
                           {
                              s1 = GetPointer<const ssa_name>(op1)->bit_values;
                           }
                           unsigned int precision;
                           precision = static_cast<unsigned int>(std::max(s0.size(), s1.size()));
                           if(GetPointer<const integer_cst>(op0))
                           {
                              const auto ic = GetPointer<const integer_cst>(op0);
                              const auto ull_value = static_cast<unsigned long long int>(tree_helper::get_integer_cst_value(ic));
                              s0 = convert_to_binary(ull_value, std::max(precision, BitLatticeManipulator::Size(op0)));
                           }
                           if(GetPointer<const integer_cst>(op1))
                           {
                              const auto ic = GetPointer<const integer_cst>(op1);
                              const auto ull_value = static_cast<unsigned long long int>(tree_helper::get_integer_cst_value(ic));
                              s1 = convert_to_binary(ull_value, std::max(precision, BitLatticeManipulator::Size(op1)));
                           }
                           precision = static_cast<unsigned int>(std::max(s0.size(), s1.size()));

                           if(precision)
                           {
                              unsigned int trailing_eq = 0;
                              unsigned int minimum_precision = static_cast<unsigned int>(std::min(s0.size(), s1.size()));
                              INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---Bit_value strings are " + s0 + " and " + s1);
                              if(precision == 0)
                              {
                                 precision = 1;
                              }
                              for(unsigned int index = 0; index < (minimum_precision - 1); ++index)
                              {
                                 if((s0[s0.size() - index - 1] == '0' || s0[s0.size() - index - 1] == 'X') && (s1[s1.size() - index - 1] == '0' || s1[s1.size() - index - 1] == 'X'))
                                 {
                                    ++trailing_eq;
                                 }
                                 else
                                 {
                                    break;
                                 }
                              }
                              if(trailing_eq)
                              {
                                 INDENT_OUT_MEX(OUTPUT_LEVEL_VERBOSE, output_level, "---Bit Value Opt: cond_expr optimized, nbits = " + STR(trailing_eq));
                                 modified = true;
                                 const auto srcp_default = ga->include_name + ":" + STR(ga->line_number) + ":" + STR(ga->column_number);
                                 const auto op0_op_type = TM->CGetTreeReindex(tree_helper::CGetType(op0)->index);
                                 const auto op1_op_type = TM->CGetTreeReindex(tree_helper::CGetType(op1)->index);

                                 if(GetPointer<ssa_name>(op0))
                                 {
                                    const auto op0_const_node = TM->CreateUniqueIntegerCst(static_cast<long long int>(trailing_eq), GET_INDEX_CONST_NODE(op0_op_type));
                                    const auto op0_expr = IRman->create_binary_operation(op0_op_type, me->op1, op0_const_node, srcp_default, rshift_expr_K);
                                    const auto op0_ga = IRman->CreateGimpleAssign(op0_op_type, nullptr, nullptr, op0_expr, function_id, B_id, srcp_default);
                                    INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "Created " + STR(op0_ga));
                                    B->PushBefore(op0_ga, stmt, AppM);
                                    const auto op0_ga_var = GetPointer<const gimple_assign>(GET_CONST_NODE(op0_ga))->op0;
                                    TM->ReplaceTreeNode(stmt, me->op1, op0_ga_var);
                                    /// set the bit_values to the ssa var
                                    auto op0_ssa = GetPointer<ssa_name>(GET_NODE(op0_ga_var));
                                    op0_ssa->bit_values = GetPointer<const ssa_name>(op0)->bit_values.substr(0, GetPointer<const ssa_name>(op0)->bit_values.size() - trailing_eq);
                                    constrainSSA(op0_ssa, TM);
                                 }
                                 else
                                 {
                                    const auto int_const = GetPointer<const integer_cst>(op0);
                                    if(tree_helper::is_int(TM, op0->index))
                                    {
                                       TM->ReplaceTreeNode(stmt, me->op1, TM->CreateUniqueIntegerCst(static_cast<long long int>(int_const->value >> trailing_eq), GET_INDEX_CONST_NODE(op0_op_type)));
                                    }
                                    else
                                    {
                                       TM->ReplaceTreeNode(stmt, me->op1, TM->CreateUniqueIntegerCst(static_cast<long long int>(static_cast<unsigned long long int>(int_const->value) >> trailing_eq), GET_INDEX_CONST_NODE(op0_op_type)));
                                    }
                                 }
                                 if(GetPointer<const ssa_name>(op1))
                                 {
                                    const auto op1_const_node = TM->CreateUniqueIntegerCst(static_cast<long long int>(trailing_eq), GET_INDEX_CONST_NODE(op1_op_type));
                                    const auto op1_expr = IRman->create_binary_operation(op1_op_type, me->op2, op1_const_node, srcp_default, rshift_expr_K);
                                    const auto op1_ga = IRman->CreateGimpleAssign(op1_op_type, tree_nodeRef(), tree_nodeRef(), op1_expr, function_id, B_id, srcp_default);
                                    INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "Created " + STR(op1_ga));
                                    B->PushBefore(op1_ga, stmt, AppM);
                                    const auto op1_ga_var = GetPointer<const gimple_assign>(GET_CONST_NODE(op1_ga))->op0;
                                    TM->ReplaceTreeNode(stmt, me->op2, op1_ga_var);
                                    /// set the bit_values to the ssa var
                                    auto op1_ssa = GetPointer<ssa_name>(GET_NODE(op1_ga_var));
                                    op1_ssa->bit_values = GetPointer<const ssa_name>(op1)->bit_values.substr(0, GetPointer<const ssa_name>(op1)->bit_values.size() - trailing_eq);
                                    constrainSSA(op1_ssa, TM);
                                 }
                                 else
                                 {
                                    const auto int_const = GetPointer<integer_cst>(op1);
                                    if(tree_helper::is_int(TM, op1->index))
                                    {
                                       TM->ReplaceTreeNode(stmt, me->op2, TM->CreateUniqueIntegerCst(static_cast<long long int>(int_const->value >> trailing_eq), GET_INDEX_CONST_NODE(op1_op_type)));
                                    }
                                    else
                                    {
                                       TM->ReplaceTreeNode(stmt, me->op2, TM->CreateUniqueIntegerCst(static_cast<long long int>(static_cast<unsigned long long int>(int_const->value) >> trailing_eq), GET_INDEX_CONST_NODE(op1_op_type)));
                                    }
                                 }
                                 const auto ssa_vd = IRman->create_ssa_name(nullptr, ga_op_type, nullptr, nullptr);
                                 auto* sn = GetPointer<ssa_name>(GET_NODE(ssa_vd));
                                 /// set the bit_values to the ssa var
                                 if(ssa->bit_values.size())
                                 {
                                    sn->bit_values = ssa->bit_values.substr(0, ssa->bit_values.size() - trailing_eq);
                                    constrainSSA(sn, TM);
                                 }
                                 const auto op_const_node = TM->CreateUniqueIntegerCst(static_cast<long long int>(trailing_eq), GET_INDEX_CONST_NODE(ga_op_type));
                                 const auto op_expr = IRman->create_binary_operation(ga_op_type, ssa_vd, op_const_node, srcp_default, lshift_expr_K);
                                 const auto curr_ga = IRman->CreateGimpleAssign(ga_op_type, ssa->min, ssa->max, op_expr, function_id, B_id, srcp_default);
                                 INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "Created " + STR(curr_ga));
                                 TM->ReplaceTreeNode(curr_ga, GetPointer<const gimple_assign>(GET_CONST_NODE(curr_ga))->op0, ga->op0);
                                 TM->ReplaceTreeNode(stmt, ga->op0, ssa_vd);
                                 B->PushAfter(curr_ga, stmt, AppM);
                                 modified = true;
                                 AppM->RegisterTransformation(GetName(), stmt);
                              }
                              else if((precision == 1 && s0 == "1" && s1 == "0") || (precision == 2 && s0 == "01" && s1 == "0"))
                              {
                                 INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---Cond expr with true and false");
                                 tree_nodeRef cond_var;
                                 if(tree_helper::is_bool(TM, GET_INDEX_CONST_NODE(ssa->type)))
                                 {
                                    cond_var = me->op0;
                                 }
                                 else
                                 {
                                    const auto ga_nop = IRman->CreateNopExpr(me->op0, ssa->type, ssa->min, ssa->max, function_id);
                                    B->PushBefore(ga_nop, stmt, AppM);
                                    modified = true;
                                    AppM->RegisterTransformation(GetName(), ga_nop);
                                    cond_var = GetPointer<const gimple_assign>(GET_CONST_NODE(ga_nop))->op0;
                                 }
                                 propagateValue(ssa, TM, ga->op0, cond_var);
                              }
                              else if(precision == 1 and s0 == "0" and s1 == "1")
                              {
                                 INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---Cond expr with false and true");
                                 /// second argument is null since we cannot add the new statement at the end of the current BB
                                 const auto new_ssa = IRman->CreateNotExpr(me->op0, blocRef(), function_id);
                                 const auto new_stmt = GetPointer<const ssa_name>(GET_CONST_NODE(new_ssa))->CGetDefStmt();
                                 B->PushBefore(new_stmt, stmt, AppM);
                                 auto type_op = GetPointer<const ternary_expr>(GET_CONST_NODE(ga->op1))->type;
                                 tree_nodeRef cond_var;
                                 if(tree_helper::is_bool(TM, GET_INDEX_CONST_NODE(type_op)))
                                 {
                                    cond_var = new_ssa;
                                 }
                                 else
                                 {
                                    const auto ga_nop = IRman->CreateNopExpr(new_ssa, type_op, ssa->min, ssa->max, function_id);
                                    B->PushBefore(ga_nop, stmt, AppM);
                                    cond_var = GetPointer<const gimple_assign>(GET_CONST_NODE(ga_nop))->op0;
                                 }
                                 propagateValue(ssa, TM, ga->op0, cond_var);
                              }
                           }
                           else if(GetPointer<const integer_cst>(op0) && GetPointer<const integer_cst>(op1))
                           {
                              const auto ic0 = GetPointer<const integer_cst>(op0);
                              const auto ull_value0 = static_cast<unsigned long long int>(tree_helper::get_integer_cst_value(ic0));
                              const auto ic1 = GetPointer<const integer_cst>(op1);
                              const auto ull_value1 = static_cast<unsigned long long int>(tree_helper::get_integer_cst_value(ic1));
                              if(ull_value0 == 1 && ull_value1 == 0 && AppM->ApplyNewTransformation())
                              {
                                 const auto type_op = GetPointer<const ternary_expr>(GET_CONST_NODE(ga->op1))->type;
                                 const auto type_op_index = GET_INDEX_CONST_NODE(type_op);
                                 tree_nodeRef cond_var;
                                 if(tree_helper::is_bool(TM, type_op_index))
                                 {
                                    cond_var = me->op0;
                                 }
                                 else
                                 {
                                    const auto ga_nop = IRman->CreateNopExpr(me->op0, type_op, TM->CreateUniqueIntegerCst(0LL, type_op_index), TM->CreateUniqueIntegerCst(1LL, type_op_index), function_id);
                                    B->PushBefore(ga_nop, stmt, AppM);
                                    cond_var = GetPointer<const gimple_assign>(GET_CONST_NODE(ga_nop))->op0;
                                 }
                                 TM->ReplaceTreeNode(stmt, ga->op1, cond_var);
                                 modified = true;
                                 AppM->RegisterTransformation(GetName(), stmt);
                              }
                           }
                        }
                     };
                     cond_expr_BVO();
                  }
#if !HAVE_STDCXX_17

                  else if(GET_CONST_NODE(ga->op1)->get_kind() == truth_not_expr_K)
                  {
                     auto tne_BVO = [&] {
                        const auto tne = GetPointer<const truth_not_expr>(GET_CONST_NODE(ga->op1));
                        if(GET_CONST_NODE(tne->op)->get_kind() == integer_cst_K)
                        {
                           const auto int_const = GetPointer<const integer_cst>(GET_CONST_NODE(tne->op));
                           const auto const_value = int_const->value == 0 ? 1LL : 0LL;
                           const auto val = TM->CreateUniqueIntegerCst(const_value, GET_INDEX_CONST_NODE(ga_op_type));
                           propagateValue(ssa, TM, ga->op0, val);
                        }
                     };
                     tne_BVO();
                  }
                  else if(GET_CONST_NODE(ga->op1)->get_kind() == truth_and_expr_K)
                  {
                     auto tae_BVO = [&] {
                        const auto tae = GetPointer<const truth_and_expr>(GET_CONST_NODE(ga->op1));
                        if(GET_CONST_NODE(tae->op0)->get_kind() == integer_cst_K || GET_CONST_NODE(tae->op1)->get_kind() == integer_cst_K || GET_INDEX_CONST_NODE(tae->op0) == GET_INDEX_CONST_NODE(tae->op1))
                        {
                           tree_nodeRef val;
                           if(GET_CONST_NODE(tae->op0)->get_kind() == integer_cst_K)
                           {
                              const auto int_const = GetPointer<const integer_cst>(GET_CONST_NODE(tae->op0));
                              if(int_const->value == 0)
                                 val = tae->op0;
                              else
                                 val = tae->op1;
                           }
                           else if(GET_CONST_NODE(tae->op1)->get_kind() == integer_cst_K)
                           {
                              const auto int_const = GetPointer<const integer_cst>(GET_CONST_NODE(tae->op1));
                              if(int_const->value == 0)
                                 val = tae->op1;
                              else
                                 val = tae->op0;
                           }
                           else
                           {
                              val = tae->op0;
                           }
                           propagateValue(ssa, TM, ga->op0, val);
                        }
                     };
                     tae_BVO();
                  }
                  else if(GET_CONST_NODE(ga->op1)->get_kind() == truth_or_expr_K)
                  {
                     auto toe_BVO = [&] {
                        const auto toe = GetPointer<const truth_or_expr>(GET_CONST_NODE(ga->op1));
                        if(GET_CONST_NODE(toe->op0)->get_kind() == integer_cst_K || GET_CONST_NODE(toe->op1)->get_kind() == integer_cst_K || GET_INDEX_CONST_NODE(toe->op0) == GET_INDEX_CONST_NODE(toe->op1))
                        {
                           tree_nodeRef val;
                           if(GET_CONST_NODE(toe->op0)->get_kind() == integer_cst_K)
                           {
                              const auto int_const = GetPointer<const integer_cst>(GET_CONST_NODE(toe->op0));
                              if(int_const->value == 0)
                                 val = toe->op1;
                              else
                                 val = toe->op0;
                           }
                           else if(GET_CONST_NODE(toe->op1)->get_kind() == integer_cst_K)
                           {
                              const auto int_const = GetPointer<const integer_cst>(GET_CONST_NODE(toe->op1));
                              if(int_const->value == 0)
                                 val = toe->op0;
                              else
                                 val = toe->op1;
                           }
                           else
                           {
                              val = toe->op0;
                           }
                           propagateValue(ssa, TM, ga->op0, val);
                        }
                     };
                     toe_BVO();
                  }
#endif
                  else if(GET_CONST_NODE(ga->op1)->get_kind() == bit_ior_expr_K)
                  {
                     auto bit_ior_expr_BVO = [&] {
                        const auto bie = GetPointer<const bit_ior_expr>(GET_CONST_NODE(ga->op1));
                        if(GET_CONST_NODE(bie->op0)->get_kind() == integer_cst_K || GET_CONST_NODE(bie->op1)->get_kind() == integer_cst_K)
                        {
                           tree_nodeRef val;
                           if(GET_CONST_NODE(bie->op0)->get_kind() == integer_cst_K)
                           {
                              const auto int_const = GetPointer<const integer_cst>(GET_CONST_NODE(bie->op0));
                              if(int_const->value == 0)
                              {
                                 val = bie->op1;
                              }
                           }
                           else
                           {
                              const auto int_const = GetPointer<const integer_cst>(GET_CONST_NODE(bie->op1));
                              if(int_const->value == 0)
                              {
                                 val = bie->op0;
                              }
                           }
                           if(val)
                           {
                              propagateValue(ssa, TM, ga->op0, val);
                           }
                        }
                     };
                     bit_ior_expr_BVO();
                  }
                  else if(GET_CONST_NODE(ga->op1)->get_kind() == pointer_plus_expr_K)
                  {
                     auto pointer_plus_expr_BVO = [&] {
                        const auto ppe = GetPointer<const pointer_plus_expr>(GET_CONST_NODE(ga->op1));
                        if(GET_CONST_NODE(ppe->op1)->get_kind() == integer_cst_K)
                        {
                           const auto int_const = GetPointer<const integer_cst>(GET_CONST_NODE(ppe->op1));
                           if(int_const->value == 0)
                           {
                              propagateValue(ssa, TM, ga->op0, ppe->op0);
                           }
                           else if(GetPointer<const ssa_name>(GET_CONST_NODE(ppe->op0)))
                           {
                              const auto temp_def = GET_CONST_NODE(GetPointer<const ssa_name>(GET_CONST_NODE(ppe->op0))->CGetDefStmt());
                              if(temp_def->get_kind() == gimple_assign_K)
                              {
                                 const auto prev_ga = GetPointer<const gimple_assign>(temp_def);
                                 if(GET_CONST_NODE(prev_ga->op1)->get_kind() == pointer_plus_expr_K)
                                 {
                                    const auto prev_ppe = GetPointer<const pointer_plus_expr>(GET_CONST_NODE(prev_ga->op1));
                                    if(GetPointer<const ssa_name>(GET_CONST_NODE(prev_ppe->op0)) && GetPointer<const integer_cst>(GET_CONST_NODE(prev_ppe->op1)))
                                    {
                                       const auto prev_val = tree_helper::get_integer_cst_value(GetPointer<const integer_cst>(GET_CONST_NODE(prev_ppe->op1)));
                                       const auto type_ppe_op1_index = tree_helper::CGetType(GET_CONST_NODE(ppe->op1))->index;
                                       const auto new_offset = TM->CreateUniqueIntegerCst((prev_val + int_const->value), type_ppe_op1_index);
                                       INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace constant usage before: " + stmt->ToString());
                                       TM->ReplaceTreeNode(stmt, ppe->op1, new_offset);
                                       TM->ReplaceTreeNode(stmt, ppe->op0, prev_ppe->op0);
                                       INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace constant usage after: " + stmt->ToString());
                                       modified = true;
                                       AppM->RegisterTransformation(GetName(), stmt);
                                    }
                                 }
                              }
                           }
                        }
                     };
                     pointer_plus_expr_BVO();
                  }
                  else if(GET_CONST_NODE(ga->op1)->get_kind() == addr_expr_K)
                  {
                     auto addr_expr_BVO = [&] {
                        const auto ae = GetPointer<const addr_expr>(GET_CONST_NODE(ga->op1));
                        const auto ae_code = GET_CONST_NODE(ae->op)->get_kind();
                        if(ae_code == mem_ref_K)
                        {
                           const auto MR = GetPointer<const mem_ref>(GET_CONST_NODE(ae->op));
                           const auto op1_val = tree_helper::get_integer_cst_value(GetPointer<const integer_cst>(GET_CONST_NODE(MR->op1)));
                           if(op1_val == 0 && GET_CONST_NODE(MR->op0)->get_kind() == ssa_name_K)
                           {
                              const auto temp_def = GET_NODE(GetPointer<const ssa_name>(GET_CONST_NODE(MR->op0))->CGetDefStmt());
                              if(temp_def->get_kind() == gimple_assign_K)
                              {
                                 const auto prev_ga = GetPointer<const gimple_assign>(temp_def);
                                 if(GET_CONST_NODE(prev_ga->op1)->get_kind() == addr_expr_K)
                                 {
                                    propagateValue(ssa, TM, ga->op0, prev_ga->op0);
                                 }
                              }
                           }
                           else if(op1_val == 0 && GET_CONST_NODE(MR->op0)->get_kind() == integer_cst_K)
                           {
                              INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace constant usage before: " + stmt->ToString());
                              TM->ReplaceTreeNode(stmt, ga->op1, MR->op0);
                              modified = true;
                              INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace constant usage after: " + stmt->ToString());
                           }
                        }
                     };
                     addr_expr_BVO();
                  }
                  else if(GET_NODE(ga->op1)->get_kind() == extract_bit_expr_K)
                  {
                     auto extract_bit_expr_BVO = [&] {
                        const auto srcp_default = ga->include_name + ":" + STR(ga->line_number) + ":" + STR(ga->column_number);
                        const auto ebe = GetPointer<const extract_bit_expr>(GET_CONST_NODE(ga->op1));
                        THROW_ASSERT(GET_CONST_NODE(ebe->op1)->get_kind() == integer_cst_K, "unexpected condition");
                        const auto pos_value = GetPointer<const integer_cst>(GET_CONST_NODE(ebe->op1))->value;
                        const auto ebe_op0_ssa = GetPointer<const ssa_name>(GET_CONST_NODE(ebe->op0));
                        if(ebe_op0_ssa)
                        {
                           if(BitLatticeManipulator::Size(ebe->op0) <= pos_value)
                           {
                              const auto right_id = GET_INDEX_CONST_NODE(ebe->op0);
                              const bool right_signed = tree_helper::is_int(TM, right_id);
                              if(right_signed)
                              {
                                 INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace extract_bit_expr usage before: " + stmt->ToString());
                                 const auto new_pos = TM->CreateUniqueIntegerCst(BitLatticeManipulator::Size(ebe->op0) - 1LL, tree_helper::CGetType(GET_CONST_NODE(ebe->op1))->index);
                                 const auto eb_op = IRman->create_extract_bit_expr(ebe->op0, new_pos, srcp_default);
                                 const auto eb_ga =
                                     IRman->CreateGimpleAssign(ebe->type, TM->CreateUniqueIntegerCst(0, GET_INDEX_CONST_NODE(ebe->type)), TM->CreateUniqueIntegerCst(1LL, GET_INDEX_CONST_NODE(ebe->type)), eb_op, function_id, B_id, srcp_default);
                                 INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---Created " + STR(eb_ga));
                                 B->PushBefore(eb_ga, stmt, AppM);
                                 const auto bit_mask_constant_node = TM->CreateUniqueIntegerCst(1, GET_INDEX_CONST_NODE(ebe->type));
                                 const auto masking = IRman->create_binary_operation(ebe->type, GetPointer<const gimple_assign>(GET_CONST_NODE(eb_ga))->op0, bit_mask_constant_node, srcp_default, truth_and_expr_K);
                                 TM->ReplaceTreeNode(stmt, ga->op1, masking); /// replaced with redundant code to restart lut_transformation
                                 INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace extract_bit_expr usage after: " + stmt->ToString());
                                 modified = true;
                                 AppM->RegisterTransformation(GetName(), stmt);
                              }
                              else
                              {
                                 INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace extract_bit_expr usage before: " + stmt->ToString());
                                 const auto zero_node = TM->CreateUniqueIntegerCst(0LL, GET_INDEX_CONST_NODE(ebe->type));
                                 propagateValue(ssa, TM, ga->op0, zero_node);
                              }
                           }
                           else if(tree_helper::is_bool(TM, GET_INDEX_CONST_NODE(ebe->op0)))
                           {
                              propagateValue(ssa, TM, ga->op0, ebe->op0);
                           }
                           else
                           {
                              auto defStmt = GET_CONST_NODE(ebe_op0_ssa->CGetDefStmt());
                              if(defStmt->get_kind() == gimple_assign_K)
                              {
                                 const auto prev_ga = GetPointer<const gimple_assign>(defStmt);
                                 auto prev_code1 = GET_CONST_NODE(prev_ga->op1)->get_kind();
                                 if(prev_code1 == nop_expr_K || prev_code1 == convert_expr_K)
                                 {
                                    auto ne = GetPointer<const unary_expr>(GET_CONST_NODE(prev_ga->op1));
                                    if(tree_helper::is_bool(TM, GET_INDEX_CONST_NODE(ne->op)))
                                    {
                                       if(pos_value == 0)
                                       {
                                          INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace extract_bit_expr usage before: " + stmt->ToString());
                                          const auto bit_mask_constant_node = TM->CreateUniqueIntegerCst(1LL, GET_INDEX_CONST_NODE(ebe->type));
                                          const auto masking = IRman->create_binary_operation(ebe->type, ne->op, bit_mask_constant_node, srcp_default, truth_and_expr_K);
                                          TM->ReplaceTreeNode(stmt, ga->op1, masking); /// replaced with redundant code to restart lut_transformation
                                          INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace extract_bit_expr usage after: " + stmt->ToString());
                                          modified = true;
                                          AppM->RegisterTransformation(GetName(), stmt);
                                       }
                                       else
                                       {
                                          const auto zero_node = TM->CreateUniqueIntegerCst(0LL, GET_INDEX_CONST_NODE(ebe->type));
                                          propagateValue(ssa, TM, ga->op0, zero_node);
                                       }
                                    }
                                    else
                                    {
                                       const auto neType_node = tree_helper::CGetType(GET_CONST_NODE(ne->op));
                                       if(neType_node->get_kind() == integer_type_K)
                                       {
                                          if(BitLatticeManipulator::Size(ne->op) > pos_value)
                                          {
                                             INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace extract_bit_expr usage before: " + stmt->ToString());
                                             const auto eb_op = IRman->create_extract_bit_expr(ne->op, ebe->op1, srcp_default);
                                             const auto eb_ga = IRman->CreateGimpleAssign(ebe->type, TM->CreateUniqueIntegerCst(0LL, GET_INDEX_CONST_NODE(ebe->type)), TM->CreateUniqueIntegerCst(1LL, GET_INDEX_CONST_NODE(ebe->type)), eb_op, function_id,
                                                                                          B_id, srcp_default);
                                             INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---Created " + STR(eb_ga));
                                             B->PushBefore(eb_ga, stmt, AppM);
                                             const auto bit_mask_constant_node = TM->CreateUniqueIntegerCst(1LL, GET_INDEX_CONST_NODE(ebe->type));
                                             const auto masking = IRman->create_binary_operation(ebe->type, GetPointer<const gimple_assign>(GET_CONST_NODE(eb_ga))->op0, bit_mask_constant_node, srcp_default, truth_and_expr_K);
                                             TM->ReplaceTreeNode(stmt, ga->op1, masking); /// replaced with redundant code to restart lut_transformation
                                             INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace extract_bit_expr usage after: " + stmt->ToString());
                                             modified = true;
                                             AppM->RegisterTransformation(GetName(), stmt);
                                          }
                                          else
                                          {
                                             const auto right_id = GET_INDEX_CONST_NODE(ne->op);
                                             const bool right_signed = tree_helper::is_int(TM, right_id);
                                             if(right_signed)
                                             {
                                                INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace extract_bit_expr usage before: " + stmt->ToString());
                                                const auto new_pos = TM->CreateUniqueIntegerCst(BitLatticeManipulator::Size(ne->op) - 1, tree_helper::CGetType(GET_CONST_NODE(ebe->op1))->index);
                                                const auto eb_op = IRman->create_extract_bit_expr(ne->op, new_pos, srcp_default);
                                                const auto eb_ga = IRman->CreateGimpleAssign(ebe->type, TM->CreateUniqueIntegerCst(0LL, GET_INDEX_CONST_NODE(ebe->type)), TM->CreateUniqueIntegerCst(1LL, GET_INDEX_CONST_NODE(ebe->type)), eb_op, function_id,
                                                                                             B_id, srcp_default);
                                                INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---Created " + STR(eb_ga));
                                                B->PushBefore(eb_ga, stmt, AppM);
                                                const auto bit_mask_constant_node = TM->CreateUniqueIntegerCst(1LL, GET_INDEX_CONST_NODE(ebe->type));
                                                const auto masking = IRman->create_binary_operation(ebe->type, GetPointer<const gimple_assign>(GET_CONST_NODE(eb_ga))->op0, bit_mask_constant_node, srcp_default, truth_and_expr_K);
                                                TM->ReplaceTreeNode(stmt, ga->op1, masking); /// replaced with redundant code to restart lut_transformation
                                                INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace extract_bit_expr usage after: " + stmt->ToString());
                                                modified = true;
                                                AppM->RegisterTransformation(GetName(), stmt);
                                             }
                                             else
                                             {
                                                INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace extract_bit_expr usage before: " + stmt->ToString());
                                                const auto zero_node = TM->CreateUniqueIntegerCst(0LL, GET_INDEX_CONST_NODE(ebe->type));
                                                propagateValue(ssa, TM, ga->op0, zero_node);
                                             }
                                          }
                                       }
                                    }
                                 }
                                 else if(prev_code1 == bit_and_expr_K)
                                 {
                                    const auto bae = GetPointer<const bit_and_expr>(GET_CONST_NODE(prev_ga->op1));
                                    if(GET_CONST_NODE(bae->op0)->get_kind() == integer_cst_K || GET_CONST_NODE(bae->op1)->get_kind() == integer_cst_K)
                                    {
                                       auto bae_op0 = bae->op0;
                                       auto bae_op1 = bae->op1;
                                       if(GET_CONST_NODE(bae->op0)->get_kind() == integer_cst_K)
                                       {
                                          std::swap(bae_op0, bae_op1);
                                       }
                                       const auto bae_mask_value = GetPointer<const integer_cst>(GET_CONST_NODE(bae_op1))->value;
                                       const auto masked_value = (bae_mask_value & (1ll << pos_value));
                                       if(masked_value && GET_CONST_NODE(bae_op0)->get_kind() != integer_cst_K)
                                       {
                                          INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace extract_bit_expr usage before: " + stmt->ToString());
                                          const auto eb_op = IRman->create_extract_bit_expr(bae_op0, ebe->op1, srcp_default);
                                          const auto eb_ga =
                                              IRman->CreateGimpleAssign(ebe->type, TM->CreateUniqueIntegerCst(0LL, GET_INDEX_CONST_NODE(ebe->type)), TM->CreateUniqueIntegerCst(1LL, GET_INDEX_CONST_NODE(ebe->type)), eb_op, function_id, B_id, srcp_default);
                                          INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---Created " + STR(eb_ga));
                                          B->PushBefore(eb_ga, stmt, AppM);
                                          const auto bit_mask_constant_node = TM->CreateUniqueIntegerCst(1, GET_INDEX_CONST_NODE(ebe->type));
                                          const auto masking = IRman->create_binary_operation(ebe->type, GetPointer<const gimple_assign>(GET_CONST_NODE(eb_ga))->op0, bit_mask_constant_node, srcp_default, truth_and_expr_K);
                                          TM->ReplaceTreeNode(stmt, ga->op1, masking); /// replaced with redundant code to restart lut_transformation
                                          INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace extract_bit_expr usage after: " + stmt->ToString());
                                          modified = true;
                                          AppM->RegisterTransformation(GetName(), stmt);
                                       }
                                       else
                                       {
                                          INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace extract_bit_expr usage before: " + stmt->ToString());
                                          const auto zero_node = TM->CreateUniqueIntegerCst(masked_value ? 1 : 0, GET_INDEX_CONST_NODE(ebe->type));
                                          propagateValue(ssa, TM, ga->op0, zero_node);
                                       }
                                    }
                                 }
                                 else if(prev_code1 == bit_ior_concat_expr_K)
                                 {
                                    const auto bice = GetPointer<const bit_ior_concat_expr>(GET_CONST_NODE(prev_ga->op1));
                                    THROW_ASSERT(GET_NODE(bice->op2)->get_kind() == integer_cst_K, "unexpected condition");
                                    const auto nbit_value = GetPointer<const integer_cst>(GET_CONST_NODE(bice->op2))->value;
                                    INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace extract_bit_expr usage before: " + stmt->ToString());
                                    const auto eb_op = IRman->create_extract_bit_expr(nbit_value > pos_value ? bice->op1 : bice->op0, ebe->op1, srcp_default);
                                    const auto eb_ga =
                                        IRman->CreateGimpleAssign(ebe->type, TM->CreateUniqueIntegerCst(0, GET_INDEX_CONST_NODE(ebe->type)), TM->CreateUniqueIntegerCst(1, GET_INDEX_CONST_NODE(ebe->type)), eb_op, function_id, B_id, srcp_default);
                                    INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---Created " + STR(eb_ga));
                                    B->PushBefore(eb_ga, stmt, AppM);
                                    const auto bit_mask_constant_node = TM->CreateUniqueIntegerCst(1, GET_INDEX_CONST_NODE(ebe->type));
                                    const auto masking = IRman->create_binary_operation(ebe->type, GetPointer<const gimple_assign>(GET_CONST_NODE(eb_ga))->op0, bit_mask_constant_node, srcp_default, truth_and_expr_K);
                                    TM->ReplaceTreeNode(stmt, ga->op1, masking); /// replaced with redundant code to restart lut_transformation
                                    INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace extract_bit_expr usage after: " + stmt->ToString());
                                    modified = true;
                                    AppM->RegisterTransformation(GetName(), stmt);
                                 }
                                 else if(prev_code1 == lshift_expr_K)
                                 {
                                    const auto lse = GetPointer<const lshift_expr>(GET_CONST_NODE(prev_ga->op1));
                                    if(GET_CONST_NODE(lse->op1)->get_kind() == integer_cst_K)
                                    {
                                       const auto lsbit_value = GetPointer<const integer_cst>(GET_CONST_NODE(lse->op1))->value;
                                       if((pos_value - lsbit_value) >= 0)
                                       {
                                          const auto new_pos = TM->CreateUniqueIntegerCst(pos_value - lsbit_value, tree_helper::CGetType(GET_CONST_NODE(ebe->op1))->index);
                                          INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace extract_bit_expr usage before: " + stmt->ToString());
                                          const auto eb_op = IRman->create_extract_bit_expr(lse->op0, new_pos, srcp_default);
                                          const auto eb_ga =
                                              IRman->CreateGimpleAssign(ebe->type, TM->CreateUniqueIntegerCst(0, GET_INDEX_CONST_NODE(ebe->type)), TM->CreateUniqueIntegerCst(1, GET_INDEX_CONST_NODE(ebe->type)), eb_op, function_id, B_id, srcp_default);
                                          INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---Created " + STR(eb_ga));
                                          B->PushBefore(eb_ga, stmt, AppM);
                                          const auto bit_mask_constant_node = TM->CreateUniqueIntegerCst(1, GET_INDEX_CONST_NODE(ebe->type));
                                          const auto masking = IRman->create_binary_operation(ebe->type, GetPointer<const gimple_assign>(GET_CONST_NODE(eb_ga))->op0, bit_mask_constant_node, srcp_default, truth_and_expr_K);
                                          TM->ReplaceTreeNode(stmt, ga->op1, masking); /// replaced with redundant code to restart lut_transformation
                                          INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace extract_bit_expr usage after: " + stmt->ToString());
                                          modified = true;
                                          AppM->RegisterTransformation(GetName(), stmt);
                                       }
                                       else
                                       {
                                          INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace extract_bit_expr usage before: " + stmt->ToString());
                                          const auto zero_node = TM->CreateUniqueIntegerCst(0LL, GET_INDEX_CONST_NODE(ebe->type));
                                          propagateValue(ssa, TM, ga->op0, zero_node);
                                       }
                                    }
                                 }
                                 else if(prev_code1 == rshift_expr_K)
                                 {
                                    const auto rse = GetPointer<const rshift_expr>(GET_CONST_NODE(prev_ga->op1));
                                    if(GET_CONST_NODE(rse->op1)->get_kind() == integer_cst_K)
                                    {
                                       const auto rsbit_value = GetPointer<const integer_cst>(GET_CONST_NODE(rse->op1))->value;
                                       THROW_ASSERT((pos_value + rsbit_value) >= 0, "unexpected condition");
                                       const auto new_pos = TM->CreateUniqueIntegerCst(pos_value + rsbit_value, tree_helper::CGetType(GET_CONST_NODE(ebe->op1))->index);
                                       INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace extract_bit_expr usage before: " + stmt->ToString());
                                       const auto eb_op = IRman->create_extract_bit_expr(rse->op0, new_pos, srcp_default);
                                       const auto eb_ga =
                                           IRman->CreateGimpleAssign(ebe->type, TM->CreateUniqueIntegerCst(0, GET_INDEX_CONST_NODE(ebe->type)), TM->CreateUniqueIntegerCst(1, GET_INDEX_CONST_NODE(ebe->type)), eb_op, function_id, B_id, srcp_default);
                                       INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---Created " + STR(eb_ga));
                                       B->PushBefore(eb_ga, stmt, AppM);
                                       const auto bit_mask_constant_node = TM->CreateUniqueIntegerCst(1, GET_INDEX_CONST_NODE(ebe->type));
                                       const auto masking = IRman->create_binary_operation(ebe->type, GetPointer<const gimple_assign>(GET_CONST_NODE(eb_ga))->op0, bit_mask_constant_node, srcp_default, truth_and_expr_K);
                                       TM->ReplaceTreeNode(stmt, ga->op1, masking); /// replaced with redundant code to restart lut_transformation
                                       INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace extract_bit_expr usage after: " + stmt->ToString());
                                       modified = true;
                                       AppM->RegisterTransformation(GetName(), stmt);
                                    }
                                    else if(GET_CONST_NODE(rse->op0)->get_kind() == integer_cst_K)
                                    {
                                       long long res_value;
                                       if(tree_helper::is_int(TM, GET_INDEX_CONST_NODE(rse->op0)))
                                       {
                                          auto val = GetPointer<const integer_cst>(GET_CONST_NODE(rse->op0))->value;
                                          val = (val >> pos_value);
                                          res_value = val;
                                       }
                                       else
                                       {
                                          auto val = static_cast<unsigned long long>(GetPointer<const integer_cst>(GET_CONST_NODE(rse->op0))->value);
                                          val = (val >> pos_value);
                                          res_value = static_cast<long long>(val);
                                       }
                                       if(res_value)
                                       {
                                          INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace extract_bit_expr usage before: " + stmt->ToString());
                                          const auto precision = BitLatticeManipulator::Size(ebe_op0_ssa->type);
                                          unsigned int log2;
                                          for(log2 = 1; precision > (1u << log2); ++log2)
                                          {
                                             ;
                                          }
                                          tree_nodeRef op1, op2, op3, op4, op5, op6, op7, op8;
                                          for(auto i = 0u; i < log2; ++i)
                                          {
                                             const auto new_pos = TM->CreateUniqueIntegerCst(i, tree_helper::CGetType(GET_CONST_NODE(ebe->op1))->index);
                                             const auto eb_op = IRman->create_extract_bit_expr(rse->op1, new_pos, srcp_default);
                                             const auto eb_ga =
                                                 IRman->CreateGimpleAssign(ebe->type, TM->CreateUniqueIntegerCst(0, GET_INDEX_CONST_NODE(ebe->type)), TM->CreateUniqueIntegerCst(1, GET_INDEX_CONST_NODE(ebe->type)), eb_op, function_id, B_id, srcp_default);
                                             INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---Created " + STR(eb_ga));
                                             B->PushBefore(eb_ga, stmt, AppM);
                                             const auto eb_ga_ssa_var = GetPointer<const gimple_assign>(GET_CONST_NODE(eb_ga))->op0;
                                             if(i == 0)
                                             {
                                                op1 = eb_ga_ssa_var;
                                             }
                                             else if(i == 1)
                                             {
                                                op2 = eb_ga_ssa_var;
                                             }
                                             else if(i == 2)
                                             {
                                                op3 = eb_ga_ssa_var;
                                             }
                                             else if(i == 3)
                                             {
                                                op4 = eb_ga_ssa_var;
                                             }
                                             else if(i == 4)
                                             {
                                                op5 = eb_ga_ssa_var;
                                             }
                                             else if(i == 5)
                                             {
                                                op6 = eb_ga_ssa_var;
                                             }
                                             else
                                             {
                                                THROW_ERROR("unexpected condition");
                                             }
                                          }
                                          const auto LutConstType = IRman->CreateDefaultUnsignedLongLongInt();

                                          const auto lut_constant_node = TM->CreateUniqueIntegerCst(res_value, GET_INDEX_CONST_NODE(LutConstType));
                                          const auto eb_op = IRman->create_lut_expr(ebe->type, lut_constant_node, op1, op2, op3, op4, op5, op6, op7, op8, srcp_default);
                                          const auto eb_ga =
                                              IRman->CreateGimpleAssign(ebe->type, TM->CreateUniqueIntegerCst(0, GET_INDEX_CONST_NODE(ebe->type)), TM->CreateUniqueIntegerCst(1, GET_INDEX_CONST_NODE(ebe->type)), eb_op, function_id, B_id, srcp_default);
                                          INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---Created " + STR(eb_ga));
                                          B->PushBefore(eb_ga, stmt, AppM);
                                          const auto bit_mask_constant_node = TM->CreateUniqueIntegerCst(1, GET_INDEX_CONST_NODE(ebe->type));
                                          const auto masking = IRman->create_binary_operation(ebe->type, GetPointer<const gimple_assign>(GET_CONST_NODE(eb_ga))->op0, bit_mask_constant_node, srcp_default, truth_and_expr_K);
                                          TM->ReplaceTreeNode(stmt, ga->op1, masking); /// replaced with redundant code to restart lut_transformation
                                          INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace extract_bit_expr usage after: " + stmt->ToString());
                                          modified = true;
                                          AppM->RegisterTransformation(GetName(), stmt);
                                       }
                                       else
                                       {
                                          const auto zero_node = TM->CreateUniqueIntegerCst(0, GET_INDEX_CONST_NODE(ebe->type));
                                          INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace extract_bit_expr usage before: " + stmt->ToString());
                                          TM->ReplaceTreeNode(stmt, ga->op1, zero_node);
                                          INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace extract_bit_expr usage after: " + stmt->ToString());
                                          modified = true;
                                          AppM->RegisterTransformation(GetName(), stmt);
                                       }
                                    }
                                 }
                                 else if(prev_code1 == bit_not_expr_K)
                                 {
                                    const auto bne = GetPointer<const bit_not_expr>(GET_CONST_NODE(prev_ga->op1));
                                    INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace extract_bit_expr usage before: " + stmt->ToString());
                                    const auto eb_op = IRman->create_extract_bit_expr(bne->op, ebe->op1, srcp_default);
                                    const auto eb_ga =
                                        IRman->CreateGimpleAssign(ebe->type, TM->CreateUniqueIntegerCst(0, GET_INDEX_CONST_NODE(ebe->type)), TM->CreateUniqueIntegerCst(1, GET_INDEX_CONST_NODE(ebe->type)), eb_op, B_id, function_id, srcp_default);
                                    INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---Created " + STR(eb_ga));
                                    B->PushBefore(eb_ga, stmt, AppM);
                                    const auto negating = IRman->create_unary_operation(ebe->type, GetPointer<const gimple_assign>(GET_CONST_NODE(eb_ga))->op0, srcp_default, truth_not_expr_K);
                                    TM->ReplaceTreeNode(stmt, ga->op1, negating);
                                    INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace extract_bit_expr usage after: " + stmt->ToString());
                                    modified = true;
                                    AppM->RegisterTransformation(GetName(), stmt);
                                 }
                                 else if(prev_code1 == bit_and_expr_K)
                                 {
                                    const auto bae = GetPointer<const bit_and_expr>(GET_CONST_NODE(prev_ga->op1));
                                    INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace extract_bit_expr usage before: " + stmt->ToString());
                                    const auto eb_op0 = IRman->create_extract_bit_expr(bae->op0, ebe->op1, srcp_default);
                                    const auto eb_ga0 =
                                        IRman->CreateGimpleAssign(ebe->type, TM->CreateUniqueIntegerCst(0, GET_INDEX_CONST_NODE(ebe->type)), TM->CreateUniqueIntegerCst(1, GET_INDEX_CONST_NODE(ebe->type)), eb_op0, function_id, B_id, srcp_default);
                                    INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---Created " + STR(eb_ga0));
                                    B->PushBefore(eb_ga0, stmt, AppM);
                                    const auto eb_op1 = IRman->create_extract_bit_expr(bae->op1, ebe->op1, srcp_default);
                                    const auto eb_ga1 =
                                        IRman->CreateGimpleAssign(ebe->type, TM->CreateUniqueIntegerCst(0, GET_INDEX_CONST_NODE(ebe->type)), TM->CreateUniqueIntegerCst(1, GET_INDEX_CONST_NODE(ebe->type)), eb_op1, function_id, B_id, srcp_default);
                                    INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---Created " + STR(eb_ga1));
                                    B->PushBefore(eb_ga1, stmt, AppM);
                                    const auto anding = IRman->create_binary_operation(ebe->type, GetPointer<const gimple_assign>(GET_CONST_NODE(eb_ga0))->op0, GetPointer<const gimple_assign>(GET_CONST_NODE(eb_ga1))->op0, srcp_default, truth_and_expr_K);
                                    TM->ReplaceTreeNode(stmt, ga->op1, anding);
                                    INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace extract_bit_expr usage after: " + stmt->ToString());
                                    modified = true;
                                    AppM->RegisterTransformation(GetName(), stmt);
                                 }
                                 else if(prev_code1 == bit_ior_expr_K)
                                 {
                                    const auto bie = GetPointer<const bit_ior_expr>(GET_CONST_NODE(prev_ga->op1));
                                    INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace extract_bit_expr usage before: " + stmt->ToString());
                                    const auto eb_op0 = IRman->create_extract_bit_expr(bie->op0, ebe->op1, srcp_default);
                                    const auto eb_ga0 =
                                        IRman->CreateGimpleAssign(ebe->type, TM->CreateUniqueIntegerCst(0, GET_INDEX_CONST_NODE(ebe->type)), TM->CreateUniqueIntegerCst(1, GET_INDEX_CONST_NODE(ebe->type)), eb_op0, function_id, B_id, srcp_default);
                                    INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---Created " + STR(eb_ga0));
                                    B->PushBefore(eb_ga0, stmt, AppM);
                                    const auto eb_op1 = IRman->create_extract_bit_expr(bie->op1, ebe->op1, srcp_default);
                                    const auto eb_ga1 =
                                        IRman->CreateGimpleAssign(ebe->type, TM->CreateUniqueIntegerCst(0, GET_INDEX_CONST_NODE(ebe->type)), TM->CreateUniqueIntegerCst(1, GET_INDEX_CONST_NODE(ebe->type)), eb_op1, function_id, B_id, srcp_default);
                                    INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---Created " + STR(eb_ga1));
                                    B->PushBefore(eb_ga1, stmt, AppM);
                                    const auto anding = IRman->create_binary_operation(ebe->type, GetPointer<const gimple_assign>(GET_CONST_NODE(eb_ga0))->op0, GetPointer<const gimple_assign>(GET_CONST_NODE(eb_ga1))->op0, srcp_default, truth_or_expr_K);
                                    TM->ReplaceTreeNode(stmt, ga->op1, anding);
                                    INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace extract_bit_expr usage after: " + stmt->ToString());
                                    modified = true;
                                    AppM->RegisterTransformation(GetName(), stmt);
                                 }
                                 else if(prev_code1 == bit_xor_expr_K)
                                 {
                                    const auto bxe = GetPointer<const bit_xor_expr>(GET_CONST_NODE(prev_ga->op1));
                                    INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace extract_bit_expr usage before: " + stmt->ToString());
                                    const auto eb_op0 = IRman->create_extract_bit_expr(bxe->op0, ebe->op1, srcp_default);
                                    const auto eb_ga0 =
                                        IRman->CreateGimpleAssign(ebe->type, TM->CreateUniqueIntegerCst(0, GET_INDEX_CONST_NODE(ebe->type)), TM->CreateUniqueIntegerCst(1, GET_INDEX_CONST_NODE(ebe->type)), eb_op0, function_id, B_id, srcp_default);
                                    INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---Created " + STR(eb_ga0));
                                    B->PushBefore(eb_ga0, stmt, AppM);
                                    const auto eb_op1 = IRman->create_extract_bit_expr(bxe->op1, ebe->op1, srcp_default);
                                    const auto eb_ga1 =
                                        IRman->CreateGimpleAssign(ebe->type, TM->CreateUniqueIntegerCst(0, GET_INDEX_CONST_NODE(ebe->type)), TM->CreateUniqueIntegerCst(1, GET_INDEX_CONST_NODE(ebe->type)), eb_op1, function_id, B_id, srcp_default);
                                    INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---Created " + STR(eb_ga1));
                                    B->PushBefore(eb_ga1, stmt, AppM);
                                    const auto anding = IRman->create_binary_operation(ebe->type, GetPointer<const gimple_assign>(GET_CONST_NODE(eb_ga0))->op0, GetPointer<const gimple_assign>(GET_CONST_NODE(eb_ga1))->op0, srcp_default, truth_xor_expr_K);
                                    TM->ReplaceTreeNode(stmt, ga->op1, anding);
                                    INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace extract_bit_expr usage after: " + stmt->ToString());
                                    modified = true;
                                    AppM->RegisterTransformation(GetName(), stmt);
                                 }
                                 else if(prev_code1 == cond_expr_K)
                                 {
                                    const auto ce = GetPointer<const cond_expr>(GET_CONST_NODE(prev_ga->op1));
                                    INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace extract_bit_expr usage before: " + stmt->ToString());
                                    const auto eb_op1 = IRman->create_extract_bit_expr(ce->op1, ebe->op1, srcp_default);
                                    const auto eb_ga1 =
                                        IRman->CreateGimpleAssign(ebe->type, TM->CreateUniqueIntegerCst(0, GET_INDEX_CONST_NODE(ebe->type)), TM->CreateUniqueIntegerCst(1, GET_INDEX_CONST_NODE(ebe->type)), eb_op1, function_id, B_id, srcp_default);
                                    INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---Created " + STR(eb_ga1));
                                    B->PushBefore(eb_ga1, stmt, AppM);
                                    const auto eb_op2 = IRman->create_extract_bit_expr(ce->op2, ebe->op1, srcp_default);
                                    const auto eb_ga2 =
                                        IRman->CreateGimpleAssign(ebe->type, TM->CreateUniqueIntegerCst(0, GET_INDEX_CONST_NODE(ebe->type)), TM->CreateUniqueIntegerCst(1, GET_INDEX_CONST_NODE(ebe->type)), eb_op2, function_id, B_id, srcp_default);
                                    INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---Created " + STR(eb_ga2));
                                    B->PushBefore(eb_ga2, stmt, AppM);
                                    const auto ceRes =
                                        IRman->create_ternary_operation(ebe->type, ce->op0, GetPointer<const gimple_assign>(GET_CONST_NODE(eb_ga1))->op0, GetPointer<const gimple_assign>(GET_CONST_NODE(eb_ga2))->op0, srcp_default, cond_expr_K);
                                    TM->ReplaceTreeNode(stmt, ga->op1, ceRes);
                                    INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace extract_bit_expr usage after: " + stmt->ToString());
                                    modified = true;
                                    AppM->RegisterTransformation(GetName(), stmt);
                                 }
                                 else if(prev_code1 == plus_expr_K)
                                 {
                                    THROW_ASSERT(GetPointer<const HLS_manager>(AppM)->get_HLS_target(), "unexpected condition");
                                    const auto hls_target = GetPointer<const HLS_manager>(AppM)->get_HLS_target();
                                    THROW_ASSERT(hls_target->get_target_device()->has_parameter("max_lut_size"), "");
                                    const auto max_lut_size = hls_target->get_target_device()->get_parameter<size_t>("max_lut_size");
                                    const auto pe = GetPointer<const plus_expr>(GET_CONST_NODE(prev_ga->op1));
                                    if(GET_CONST_NODE(pe->op1)->get_kind() == integer_cst_K && BitLatticeManipulator::Size(ebe->op0) <= max_lut_size)
                                    {
                                       INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace extract_bit_expr usage before: " + stmt->ToString());
                                       auto carry = TM->CreateUniqueIntegerCst(0, GET_INDEX_CONST_NODE(ebe->type));
                                       tree_nodeRef sum;
                                       for(long long int bitIndex = 0; bitIndex <= pos_value; ++bitIndex)
                                       {
                                          const auto bitIndex_node = TM->CreateUniqueIntegerCst(bitIndex, tree_helper::CGetType(GET_CONST_NODE(ebe->op1))->index);
                                          const auto eb_op1 = IRman->create_extract_bit_expr(pe->op0, bitIndex_node, srcp_default);
                                          const auto eb_ga1 =
                                              IRman->CreateGimpleAssign(ebe->type, TM->CreateUniqueIntegerCst(0, GET_INDEX_CONST_NODE(ebe->type)), TM->CreateUniqueIntegerCst(1, GET_INDEX_CONST_NODE(ebe->type)), eb_op1, function_id, B_id, srcp_default);
                                          INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---Created " + STR(eb_ga1));
                                          B->PushBefore(eb_ga1, stmt, AppM);
                                          const auto eb_op2 = IRman->create_extract_bit_expr(pe->op1, bitIndex_node, srcp_default);
                                          const auto eb_ga2 =
                                              IRman->CreateGimpleAssign(ebe->type, TM->CreateUniqueIntegerCst(0, GET_INDEX_CONST_NODE(ebe->type)), TM->CreateUniqueIntegerCst(1, GET_INDEX_CONST_NODE(ebe->type)), eb_op2, function_id, B_id, srcp_default);
                                          INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---Created " + STR(eb_ga2));
                                          B->PushBefore(eb_ga2, stmt, AppM);
                                          const auto sum0 =
                                              IRman->create_binary_operation(ebe->type, GetPointer<const gimple_assign>(GET_CONST_NODE(eb_ga1))->op0, GetPointer<const gimple_assign>(GET_CONST_NODE(eb_ga2))->op0, srcp_default, truth_xor_expr_K);
                                          const auto sum0_ga1 =
                                              IRman->CreateGimpleAssign(ebe->type, TM->CreateUniqueIntegerCst(0, GET_INDEX_CONST_NODE(ebe->type)), TM->CreateUniqueIntegerCst(1, GET_INDEX_CONST_NODE(ebe->type)), sum0, function_id, B_id, srcp_default);
                                          INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---Created " + STR(sum0_ga1));
                                          B->PushBefore(sum0_ga1, stmt, AppM);
                                          sum = IRman->create_binary_operation(ebe->type, GetPointer<const gimple_assign>(GET_CONST_NODE(sum0_ga1))->op0, carry, srcp_default, truth_xor_expr_K);
                                          if(bitIndex < pos_value)
                                          {
                                             const auto sum_ga1 =
                                                 IRman->CreateGimpleAssign(ebe->type, TM->CreateUniqueIntegerCst(0, GET_INDEX_CONST_NODE(ebe->type)), TM->CreateUniqueIntegerCst(1, GET_INDEX_CONST_NODE(ebe->type)), sum, function_id, B_id, srcp_default);
                                             INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---Created " + STR(sum_ga1));
                                             B->PushBefore(sum_ga1, stmt, AppM);

                                             const auto and1 = IRman->create_binary_operation(ebe->type, GetPointer<const gimple_assign>(GET_CONST_NODE(eb_ga1))->op0, carry, srcp_default, truth_and_expr_K);
                                             const auto and1_ga =
                                                 IRman->CreateGimpleAssign(ebe->type, TM->CreateUniqueIntegerCst(0, GET_INDEX_CONST_NODE(ebe->type)), TM->CreateUniqueIntegerCst(1, GET_INDEX_CONST_NODE(ebe->type)), and1, function_id, B_id, srcp_default);
                                             INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---Created " + STR(and1_ga));
                                             B->PushBefore(and1_ga, stmt, AppM);

                                             const auto and2 = IRman->create_binary_operation(ebe->type, GetPointer<const gimple_assign>(GET_CONST_NODE(eb_ga2))->op0, carry, srcp_default, truth_and_expr_K);
                                             const auto and2_ga =
                                                 IRman->CreateGimpleAssign(ebe->type, TM->CreateUniqueIntegerCst(0, GET_INDEX_CONST_NODE(ebe->type)), TM->CreateUniqueIntegerCst(1, GET_INDEX_CONST_NODE(ebe->type)), and2, function_id, B_id, srcp_default);
                                             INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---Created " + STR(and2_ga));
                                             B->PushBefore(and2_ga, stmt, AppM);

                                             const auto and3 =
                                                 IRman->create_binary_operation(ebe->type, GetPointer<const gimple_assign>(GET_CONST_NODE(eb_ga1))->op0, GetPointer<const gimple_assign>(GET_CONST_NODE(eb_ga2))->op0, srcp_default, truth_and_expr_K);
                                             const auto and3_ga =
                                                 IRman->CreateGimpleAssign(ebe->type, TM->CreateUniqueIntegerCst(0, GET_INDEX_CONST_NODE(ebe->type)), TM->CreateUniqueIntegerCst(1, GET_INDEX_CONST_NODE(ebe->type)), and3, function_id, B_id, srcp_default);
                                             INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---Created " + STR(and3_ga));
                                             B->PushBefore(and3_ga, stmt, AppM);

                                             const auto or1 =
                                                 IRman->create_binary_operation(ebe->type, GetPointer<const gimple_assign>(GET_CONST_NODE(and1_ga))->op0, GetPointer<const gimple_assign>(GET_CONST_NODE(and2_ga))->op0, srcp_default, truth_or_expr_K);
                                             const auto or1_ga =
                                                 IRman->CreateGimpleAssign(ebe->type, TM->CreateUniqueIntegerCst(0, GET_INDEX_CONST_NODE(ebe->type)), TM->CreateUniqueIntegerCst(1, GET_INDEX_CONST_NODE(ebe->type)), or1, function_id, B_id, srcp_default);
                                             INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---Created " + STR(or1_ga));
                                             B->PushBefore(or1_ga, stmt, AppM);

                                             const auto carry1 =
                                                 IRman->create_binary_operation(ebe->type, GetPointer<const gimple_assign>(GET_CONST_NODE(or1_ga))->op0, GetPointer<const gimple_assign>(GET_CONST_NODE(and3_ga))->op0, srcp_default, truth_or_expr_K);
                                             const auto carry1_ga =
                                                 IRman->CreateGimpleAssign(ebe->type, TM->CreateUniqueIntegerCst(0, GET_INDEX_CONST_NODE(ebe->type)), TM->CreateUniqueIntegerCst(1, GET_INDEX_CONST_NODE(ebe->type)), carry1, function_id, B_id, srcp_default);
                                             INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---Created " + STR(carry1_ga));
                                             B->PushBefore(carry1_ga, stmt, AppM);
                                             carry = GetPointer<const gimple_assign>(GET_CONST_NODE(carry1_ga))->op0;
                                          }
                                       }
                                       TM->ReplaceTreeNode(stmt, ga->op1, sum);
                                       INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace extract_bit_expr usage after: " + stmt->ToString());
                                       modified = true;
                                       AppM->RegisterTransformation(GetName(), stmt);
                                    }
                                 }
                              }
                           }
                        }
                        else if(GET_CONST_NODE(ebe->op0)->get_kind() == integer_cst_K)
                        {
                           bool res_value;
                           if(tree_helper::is_int(TM, GET_INDEX_CONST_NODE(ebe->op0)))
                           {
                              auto val = GetPointer<const integer_cst>(GET_CONST_NODE(ebe->op0))->value;
                              val = (val >> pos_value) & 1;
                              res_value = val;
                           }
                           else
                           {
                              auto val = static_cast<unsigned long long>(GetPointer<const integer_cst>(GET_CONST_NODE(ebe->op0))->value);
                              val = (val >> pos_value) & 1;
                              res_value = val;
                           }
                           const auto res_node = TM->CreateUniqueIntegerCst(res_value, GET_INDEX_CONST_NODE(ebe->type));
                           propagateValue(ssa, TM, ga->op0, res_node);
                        }
                     };
                     extract_bit_expr_BVO();
                  }
                  else if(GET_CONST_NODE(ga->op1)->get_kind() == nop_expr_K)
                  {
                     auto nop_expr_BVO = [&] {
                        const auto ne = GetPointer<const nop_expr>(GET_CONST_NODE(ga->op1));
                        if(tree_helper::is_bool(TM, GET_INDEX_CONST_NODE(ga->op0)))
                        {
                           if(tree_helper::is_bool(TM, GET_INDEX_CONST_NODE(ne->op)))
                           {
                              propagateValue(ssa, TM, ga->op0, ne->op);
                           }
                           else
                           {
                              const auto ne_op_ssa = GetPointer<const ssa_name>(GET_CONST_NODE(ne->op));
                              if(ne_op_ssa)
                              {
                                 if(GET_CONST_NODE(ne_op_ssa->type)->get_kind() == integer_type_K)
                                 {
                                    INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace extract_bit_expr usage before: " + stmt->ToString());
                                    const auto indexType = IRman->CreateDefaultUnsignedLongLongInt();
                                    const auto zero_node = TM->CreateUniqueIntegerCst(0, GET_INDEX_CONST_NODE(indexType));
                                    const auto srcp_default = ga->include_name + ":" + STR(ga->line_number) + ":" + STR(ga->column_number);
                                    const auto eb_op = IRman->create_extract_bit_expr(ne->op, zero_node, srcp_default);
                                    const auto eb_ga =
                                        IRman->CreateGimpleAssign(ne->type, TM->CreateUniqueIntegerCst(0, GET_INDEX_CONST_NODE(ne->type)), TM->CreateUniqueIntegerCst(1, GET_INDEX_CONST_NODE(ne->type)), eb_op, function_id, B_id, srcp_default);
                                    INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---Created " + STR(eb_ga));
                                    B->PushBefore(eb_ga, stmt, AppM);
                                    const auto bit_mask_constant_node = TM->CreateUniqueIntegerCst(1, GET_INDEX_CONST_NODE(ne->type));
                                    const auto masking = IRman->create_binary_operation(ne->type, GetPointer<const gimple_assign>(GET_CONST_NODE(eb_ga))->op0, bit_mask_constant_node, srcp_default, truth_and_expr_K);
                                    TM->ReplaceTreeNode(stmt, ga->op1, masking); /// replaced with redundant code to restart lut_transformation
                                    INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---replace extract_bit_expr usage after: " + stmt->ToString());
                                    modified = true;
                                    AppM->RegisterTransformation(GetName(), stmt);
                                 }
                              }
                           }
                        }
                     };
                     nop_expr_BVO();
                  }
               };
               ga_BVO();
            }
         }
         INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "<--Statement analyzed " + GET_NODE(stmt)->ToString());
      }
      INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "<--BB analyzed " + STR(B_id));
      for(const auto& phi : B->CGetPhiList())
      {
         INDENT_DBG_MEX(DEBUG_LEVEL_PEDANTIC, debug_level, "Phi operation " + GET_NODE(phi)->ToString());
         INDENT_DBG_MEX(DEBUG_LEVEL_PEDANTIC, debug_level, "Phi index: " + STR(GET_INDEX_CONST_NODE(phi)));
         auto pn = GetPointerS<gimple_phi>(GET_CONST_NODE(phi));
         bool is_virtual = pn->virtual_flag;
         if(not is_virtual)
         {
            INDENT_DBG_MEX(DEBUG_LEVEL_PEDANTIC, debug_level, "LHS: " + STR(GET_INDEX_CONST_NODE(pn->res)));
            const auto ssa = GetPointer<const ssa_name>(GET_CONST_NODE(pn->res));
            if(ssa)
            {
               const auto& bit_values = ssa->bit_values;
               const auto is_constant = is_bit_values_constant(bit_values);
               if(is_constant)
               {
                  INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "---Left part is constant " + bit_values);
                  const auto const_value = convert_bitvalue2longlong(bit_values, TM, GET_INDEX_CONST_NODE(pn->res));
                  const auto type_index = tree_helper::CGetType(GET_CONST_NODE(pn->res))->index;
                  const auto val = TM->CreateUniqueIntegerCst(static_cast<long long int>(const_value), type_index);

                  propagateValue(ssa, TM, pn->res, val);
                  if(AppM->ApplyNewTransformation())
                  {
                     pn->res = TM->GetTreeReindex(ssa->index);
                     THROW_ASSERT(ssa->CGetUseStmts().empty(), "unexpected case");
                     AppM->RegisterTransformation(GetName(), phi);
                  }
               }
            }
         }
      }
   }
}

const CustomUnorderedSet<std::pair<FrontendFlowStepType, FrontendFlowStep::FunctionRelationship>> Bit_Value_opt2::ComputeFrontendRelationships(const DesignFlowStep::RelationshipType relationship_type) const
{
   CustomUnorderedSet<std::pair<FrontendFlowStepType, FunctionRelationship>> relationships;
   switch(relationship_type)
   {
      case DEPENDENCE_RELATIONSHIP:
      {
         relationships.insert(std::make_pair(USE_COUNTING, SAME_FUNCTION));
         if(!parameters->getOption<int>(OPT_gcc_openmp_simd))
         {
            relationships.insert(std::make_pair(BIT_VALUE_OPT, SAME_FUNCTION));
            relationships.insert(std::make_pair(BIT_VALUE, SAME_FUNCTION));
         }
         relationships.insert(std::make_pair(RANGE_ANALYSIS, WHOLE_APPLICATION));
         relationships.insert(std::make_pair(BIT_VALUE_OPT2, CALLED_FUNCTIONS));
         break;
      }
      case(PRECEDENCE_RELATIONSHIP):
      {
         break;
      }
      case(INVALIDATION_RELATIONSHIP):
      {
         if(GetStatus() == DesignFlowStep_Status::SUCCESS)
         {
            if(!parameters->getOption<int>(OPT_gcc_openmp_simd))
            {
               relationships.insert(std::make_pair(BIT_VALUE, SAME_FUNCTION));
            }
         }
         break;
      }
      default:
         THROW_UNREACHABLE("");
   }
   return relationships;
}

Bit_Value_opt2::Bit_Value_opt2(const ParameterConstRef _parameters, const application_managerRef _AppM, unsigned int _function_id, const DesignFlowManagerConstRef _design_flow_manager)
    : FunctionFrontendFlowStep(_AppM, _function_id, BIT_VALUE_OPT2, _design_flow_manager, _parameters)
{
   debug_level = parameters->get_class_debug_level(GET_CLASS(*this), DEBUG_LEVEL_NONE);
}

Bit_Value_opt2::~Bit_Value_opt2() = default;

DesignFlowStep_Status Bit_Value_opt2::InternalExec()
{
   const auto design_flow_step = GetPointer<const FrontendFlowStepFactory>(design_flow_manager.lock()->CGetDesignFlowStepFactory("Frontend"))->CreateFunctionFrontendFlowStep(FrontendFlowStepType::BIT_VALUE_OPT, function_id);
   design_flow_step->Initialize();
   const auto return_status = design_flow_step->Exec();
   return_status == DesignFlowStep_Status::SUCCESS ? function_behavior->UpdateBBVersion() : 0;
   return return_status;
}

bool Bit_Value_opt2::HasToBeExecuted() const
{
   return (FunctionFrontendFlowStep::HasToBeExecuted() || bitvalue_version != function_behavior->GetBitValueVersion());
}
