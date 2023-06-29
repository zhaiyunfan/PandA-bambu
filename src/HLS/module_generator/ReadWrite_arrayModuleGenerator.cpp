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
 *              Copyright (C) 2022-2023 Politecnico di Milano
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
 * @file ReadWrite_arrayModuleGenerator.cpp
 * @brief
 *
 *
 *
 * @author Michele Fiorito <michele.fiorito@polimi.it>
 * @author Fabrizio Ferrandi <fabrizio.ferrandi@polimi.it>
 * $Revision$
 * $Date$
 * Last modified by $Author$
 *
 */

#include "ReadWrite_arrayModuleGenerator.hpp"

#include "behavioral_helper.hpp"
#include "call_graph_manager.hpp"
#include "constant_strings.hpp"
#include "function_behavior.hpp"
#include "hls_manager.hpp"
#include "language_writer.hpp"
#include "math_function.hpp"
#include "op_graph.hpp"
#include "tree_helper.hpp"
#include "tree_manager.hpp"

ReadWrite_arrayModuleGenerator::ReadWrite_arrayModuleGenerator(const HLS_managerRef& _HLSMgr) : Registrar(_HLSMgr)
{
}

void ReadWrite_arrayModuleGenerator::InternalExec(std::ostream& out, structural_objectRef /* mod */,
                                                  unsigned int function_id, vertex op_v,
                                                  const HDLWriter_Language /* language */,
                                                  const std::vector<ModuleGenerator::parameter>& /* _p */,
                                                  const std::vector<ModuleGenerator::parameter>& _ports_in,
                                                  const std::vector<ModuleGenerator::parameter>& _ports_out,
                                                  const std::vector<ModuleGenerator::parameter>& /* _ports_inout */)
{
   const auto fname = [&]() {
      const auto FB = HLSMgr->CGetFunctionBehavior(function_id);
      if(op_v)
      {
         const auto cfg = FB->CGetOpGraph(FunctionBehavior::CFG);
         return cfg->CGetOpNodeInfo(op_v)->GetOperation();
      }
      return FB->CGetBehavioralHelper()->get_function_name();
   }();
   THROW_ASSERT(fname.find(STR_CST_interface_parameter_keyword) != std::string::npos,
                "Unexpected array interface module name");
   const auto parameter_name = fname.substr(0, fname.find(STR_CST_interface_parameter_keyword));
   auto foundParam = false;
   auto arraySize = 1U;
   auto factor = 1U;
   const auto TM = HLSMgr->get_tree_manager();
   const auto top_functions = HLSMgr->CGetCallGraphManager()->GetRootFunctions();

   for(const auto& f_props : HLSMgr->design_attributes)
   {
      const auto& name = f_props.first;
      const auto& props = f_props.second;
      const auto id = TM->function_index_mngl(name);
      if(top_functions.count(id) && props.find(parameter_name) != props.end() &&
         props.at(parameter_name).find(attr_size) != props.at(parameter_name).end() && !foundParam)
      {
         arraySize = boost::lexical_cast<decltype(arraySize)>(props.at(parameter_name).at(attr_size));
         factor = boost::lexical_cast<decltype(factor)>(props.at(parameter_name).at(attr_interface_factor));
         foundParam = true;
      }
      else if(foundParam)
      {
         THROW_ERROR("At least two top functions have the same array parameter");
      }
   }

   const auto isAlignedPowerOfTwo = _ports_out[1].alignment == ceil_pow2(_ports_out[1].alignment);
   const auto addressMaxValue = factor * _ports_out[1].alignment * arraySize - 1U;
   const auto nbitAddress =
       addressMaxValue <= 1U ? 1U : (64u - static_cast<unsigned>(__builtin_clzll(addressMaxValue)));

   out << "//" << (isAlignedPowerOfTwo ? "T" : "F") << "\n";
   out << "assign " << _ports_out[2].name << " = " << _ports_in[2].name << "[0];\n";

   if(isAlignedPowerOfTwo)
   {
      out << "assign " << _ports_out[1].name << " = " << _ports_in[6].name << "[BITSIZE_" << _ports_in[6].name
          << "*0+:" << nbitAddress << "] / " << _ports_out[1].alignment << ";\n";
   }
   else
   {
      out << "assign " << _ports_out[1].name << " = " << _ports_in[6].name << "[2+BITSIZE_" << _ports_in[6].name
          << "*0+:" << nbitAddress - 2U << "] / " << _ports_out[1].alignment / 4 << ";\n";
   }

   if(_ports_in.size() == 8U)
   {
      out << "assign " << _ports_out[0].name << "[BITSIZE_" << _ports_out[0].name << "*0+:BITSIZE_"
          << _ports_out[0].name << "] = " << _ports_in[7].name << ";\n";
   }

   if(_ports_out.size() == 5U)
   {
      out << "assign " << _ports_out[3].name << " = " << _ports_in[2].name << "[0] & (|" << _ports_in[3].name
          << "[BITSIZE_" << _ports_in[3].name << "*0+:BITSIZE_" << _ports_in[3].name << "]);\n";
      out << "assign " << _ports_out[4].name << " = " << _ports_in[5].name << "[BITSIZE_" << _ports_in[5].name
          << "*0+:BITSIZE_" << _ports_in[5].name << "];\n";
   }
}
