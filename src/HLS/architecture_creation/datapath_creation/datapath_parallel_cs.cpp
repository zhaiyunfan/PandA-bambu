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
 *              Copyright (c) 2016-2021 Politecnico di Milano
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
 * @file classic_datapath.cpp
 * @brief Datapath for context switch
 *
 * @author Nicola Saporetti <nicola.saporetti@gmail.com>
 *
 */

#include "datapath_parallel_cs.hpp"
#include "BambuParameter.hpp"
#include "behavioral_helper.hpp"
#include "copyrights_strings.hpp"
#include "hls.hpp"
#include "hls_manager.hpp"
#include "hls_target.hpp"
#include "loop.hpp"
#include "loops.hpp"
#include "memory.hpp"
#include "memory_cs.hpp"
#include "structural_manager.hpp"
#include "structural_objects.hpp"
#include "technology_manager.hpp"

/// HLS/function_allocation include
#include "omp_functions.hpp"

/// STD include
#include <cmath>
#include <list>
#include <string>
#include <tuple>

/// utility includes
#include "custom_set.hpp"
#include "dbgPrintHelper.hpp"
#include "math_function.hpp"
#include "utility.hpp"

datapath_parallel_cs::datapath_parallel_cs(const ParameterConstRef _parameters, const HLS_managerRef _HLSMgr, unsigned int _funId, const DesignFlowManagerConstRef _design_flow_manager, const HLSFlowStep_Type _hls_flow_step_type)
    : classic_datapath(_parameters, _HLSMgr, _funId, _design_flow_manager, _hls_flow_step_type)
{
   debug_level = parameters->get_class_debug_level(GET_CLASS(*this));
}

// HLSFunctionStep(_parameters, _HLSMgr, _funId, _design_flow_manager, _hls_flow_step_type)

datapath_parallel_cs::~datapath_parallel_cs()
{
}

const CustomUnorderedSet<std::tuple<HLSFlowStep_Type, HLSFlowStepSpecializationConstRef, HLSFlowStep_Relationship>> datapath_parallel_cs::ComputeHLSRelationships(const DesignFlowStep::RelationshipType relationship_type) const
{
   CustomUnorderedSet<std::tuple<HLSFlowStep_Type, HLSFlowStepSpecializationConstRef, HLSFlowStep_Relationship>> ret;
   switch(relationship_type)
   {
      case DEPENDENCE_RELATIONSHIP:
      {
         ret.insert(std::make_tuple(HLSFlowStep_Type::OMP_BODY_LOOP_SYNTHESIS_FLOW, HLSFlowStepSpecializationConstRef(), HLSFlowStep_Relationship::CALLED_FUNCTIONS));
         ret.insert(std::make_tuple(HLSFlowStep_Type::INITIALIZE_HLS, HLSFlowStepSpecializationConstRef(), HLSFlowStep_Relationship::SAME_FUNCTION));
         break;
      }
      case INVALIDATION_RELATIONSHIP:
      {
         break;
      }
      case PRECEDENCE_RELATIONSHIP:
      {
         break;
      }
      default:
         THROW_UNREACHABLE("");
   }
   return ret;
}

DesignFlowStep_Status datapath_parallel_cs::InternalExec()
{
   /// main circuit type
   const FunctionBehaviorConstRef FB = HLSMgr->CGetFunctionBehavior(funId);
   structural_type_descriptorRef module_type = structural_type_descriptorRef(new structural_type_descriptor("datapath_" + FB->CGetBehavioralHelper()->get_function_name()));
   /// top circuit creation
   HLS->datapath = structural_managerRef(new structural_manager(HLS->Param));
   HLS->datapath->set_top_info("Datapath_i", module_type);
   const structural_objectRef datapath_cir = HLS->datapath->get_circ();

   // Now the top circuit is created, just as an empty box. <circuit> is a reference to the structural object that
   // will contain all the circuit components
   datapath_cir->set_black_box(false);

   /// Set some descriptions and legal stuff
   GetPointer<module>(datapath_cir)->set_description("Datapath RTL descrition for " + FB->CGetBehavioralHelper()->get_function_name());
   GetPointer<module>(datapath_cir)->set_copyright(GENERATED_COPYRIGHT);
   GetPointer<module>(datapath_cir)->set_authors("Component automatically generated by bambu");
   GetPointer<module>(datapath_cir)->set_license(GENERATED_LICENSE);

   /// add clock and reset to circuit. It increments in_port number and update in_port_map
   INDENT_DBG_MEX(DEBUG_LEVEL_VERBOSE, debug_level, "---Adding clock and reset ports");
   structural_objectRef clock, reset;
   add_clock_reset(clock, reset);

   /// add all input ports
   INDENT_DBG_MEX(DEBUG_LEVEL_VERBOSE, debug_level, "---Adding ports for primary inputs and outputs");
   add_ports();

   instantiate_component_parallel(clock, reset);

   CustomOrderedSet<structural_objectRef> memory_modules;
   const structural_managerRef& SM = this->HLS->datapath;
   const structural_objectRef circuit = SM->get_circ();
   auto omp_functions = GetPointer<OmpFunctions>(HLSMgr->Rfuns);
   const auto kernel_functions = omp_functions->kernel_functions;
#ifndef NDEBUG
   if(kernel_functions.size() > 1)
   {
      for(const auto kernel_function : kernel_functions)
         INDENT_DBG_MEX(DEBUG_LEVEL_NONE, debug_level, "Kernel function " + HLSMgr->CGetFunctionBehavior(kernel_function)->CGetBehavioralHelper()->get_function_name());
      THROW_UNREACHABLE("More than one kernel function");
   }
#endif
   const auto kernel_function_id = *(kernel_functions.begin());
   const auto kernel_function_name = HLSMgr->CGetFunctionBehavior(kernel_function_id)->CGetBehavioralHelper()->get_function_name();
   std::string kernel_library = HLS->HLS_T->get_technology_manager()->get_library(kernel_function_name);
   structural_objectRef kernel_mod;
   int addr_kernel = ceil_log2(parameters->getOption<unsigned long long>(OPT_num_accelerators));
   if(!addr_kernel)
   {
      addr_kernel = 1;
   }
   for(unsigned int i = 0; i < parameters->getOption<unsigned int>(OPT_num_accelerators); ++i)
   {
      std::string kernel_module_name = kernel_function_name + "_" + STR(i);
      kernel_mod = SM->add_module_from_technology_library(kernel_module_name, kernel_function_name, kernel_library, circuit, HLS->HLS_T->get_technology_manager());
      memory_modules.insert(kernel_mod);
      connect_module_kernel(kernel_mod, i);
      // setting num of kernel in each scheduler
      GetPointer<module>(kernel_mod)->SetParameter("KERN_NUM", STR(addr_kernel) + "'d" + STR(i)); // add num_kernel to kernel
   }
   manage_extern_global_port_parallel(SM, memory_modules, datapath_cir);
   memory::propagate_memory_parameters(const_cast<structural_objectRef&>(kernel_mod), SM); // propagate memory_parameter to datapath_parallel

   for(unsigned int i = 0; i < parameters->getOption<unsigned int>(OPT_num_accelerators); ++i)
   {
      kernel_mod = circuit->find_member(kernel_function_name + "_" + STR(i), component_o_K, circuit);
      THROW_ASSERT(kernel_mod, "");
      connect_i_module_kernel(kernel_mod);
   }
   return DesignFlowStep_Status::SUCCESS;
}

void datapath_parallel_cs::add_ports()
{
   classic_datapath::add_ports(); // add standard port
   PRINT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "Added standard port kernel");
   const structural_managerRef& SM = this->HLS->datapath;
   const structural_objectRef circuit = SM->get_circ();
   auto num_thread = parameters->getOption<unsigned int>(OPT_num_accelerators);
   structural_type_descriptorRef bool_type = structural_type_descriptorRef(new structural_type_descriptor("bool", 0));
   PRINT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "Start adding new ports");
   SM->add_port_vector(STR(DONE_PORT_NAME) + "_accelerator", port_o::OUT, num_thread, circuit, bool_type);
   SM->add_port_vector(STR(DONE_REQUEST) + "_accelerator", port_o::OUT, num_thread, circuit, bool_type);
   SM->add_port_vector(STR(START_PORT_NAME) + "_accelerator", port_o::IN, num_thread, circuit, bool_type);

   SM->add_port(STR(TASKS_POOL_END), port_o::IN, circuit, bool_type);
   structural_type_descriptorRef request_type = structural_type_descriptorRef(new structural_type_descriptor("bool", 32));
   SM->add_port("request", port_o::IN, circuit, request_type);
   PRINT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "New ports added");
}

void datapath_parallel_cs::connect_module_kernel(structural_objectRef kernel_mod, unsigned int num_kernel)
{
   const structural_managerRef SM = this->HLS->datapath;
   const structural_objectRef circuit = SM->get_circ();
   const FunctionBehaviorConstRef FB = HLSMgr->CGetFunctionBehavior(funId);
   const BehavioralHelperConstRef BH = FB->CGetBehavioralHelper();
   std::string prefix = "in_port_";

   structural_objectRef clock_kernel = kernel_mod->find_member(CLOCK_PORT_NAME, port_o_K, kernel_mod);
   structural_objectRef clock_datapath = circuit->find_member(CLOCK_PORT_NAME, port_o_K, circuit);
   SM->add_connection(clock_datapath, clock_kernel);
   PRINT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, " - Connected clock port");

   structural_objectRef reset_kernel = kernel_mod->find_member(RESET_PORT_NAME, port_o_K, kernel_mod);
   structural_objectRef reset_datapath = circuit->find_member(RESET_PORT_NAME, port_o_K, circuit);
   SM->add_connection(reset_datapath, reset_kernel);
   PRINT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, " - Connected reset port");

   const std::list<unsigned int>& function_parameters = BH->get_parameters();
   for(auto const function_parameter : function_parameters)
   {
      structural_objectRef parameter_kernel = kernel_mod->find_member(BH->PrintVariable(function_parameter), port_o_K, kernel_mod);
      structural_objectRef parameter_datapath = circuit->find_member(prefix + BH->PrintVariable(function_parameter), port_o_K, circuit);
      if(parameter_datapath == nullptr)
      {
         structural_type_descriptorRef request_type = structural_type_descriptorRef(new structural_type_descriptor("bool", 32));
         SM->add_port(prefix + BH->PrintVariable(function_parameter), port_o::IN, circuit, request_type);
         parameter_datapath = circuit->find_member(prefix + BH->PrintVariable(function_parameter), port_o_K, circuit);
      }
      std::cout << "Parameter: " << BH->PrintVariable(function_parameter) << std::endl;
      if(parameter_kernel != nullptr)
      {
         SM->add_connection(parameter_datapath, parameter_kernel);
      }
   }
   PRINT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, " - Connected parameter port");

   structural_objectRef task_pool_kernel = kernel_mod->find_member(TASKS_POOL_END, port_o_K, kernel_mod);
   structural_objectRef task_pool_datapath = circuit->find_member(TASKS_POOL_END, port_o_K, circuit);
   SM->add_connection(task_pool_datapath, task_pool_kernel);
   PRINT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, " - Connected task_pool_end");

   structural_objectRef start_kernel = kernel_mod->find_member(START_PORT_NAME, port_o_K, kernel_mod);
   structural_objectRef start_datapath = circuit->find_member(STR(START_PORT_NAME) + "_accelerator", port_vector_o_K, circuit);
   SM->add_connection(start_kernel, GetPointer<port_o>(start_datapath)->get_port(num_kernel));
   PRINT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, " - Connected start");

   structural_objectRef done_kernel = kernel_mod->find_member(DONE_PORT_NAME, port_o_K, kernel_mod);
   structural_objectRef done_datapath = circuit->find_member(STR(DONE_PORT_NAME) + "_accelerator", port_vector_o_K, circuit);
   SM->add_connection(done_kernel, GetPointer<port_o>(done_datapath)->get_port(num_kernel));
   PRINT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, " - Connected done");

   structural_objectRef done_req_kernel = kernel_mod->find_member(DONE_REQUEST, port_o_K, kernel_mod);
   structural_objectRef done_req_datapath = circuit->find_member(STR(DONE_REQUEST) + "_accelerator", port_vector_o_K, circuit);
   SM->add_connection(done_req_kernel, GetPointer<port_o>(done_req_datapath)->get_port(num_kernel));
   PRINT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, " - Connected done_req");
}

void datapath_parallel_cs::connect_i_module_kernel(structural_objectRef kernel_mod)
{
   const structural_managerRef SM = this->HLS->datapath;
   const structural_objectRef circuit = SM->get_circ();
   structural_objectRef request_datapath = circuit->find_member("request", port_o_K, circuit);
   for(unsigned int j = 0; j < GetPointer<module>(kernel_mod)->get_in_port_size(); j++) // find i
   {
      structural_objectRef port_i = GetPointer<module>(kernel_mod)->get_in_port(j);
      structural_objectRef connectedPort = GetPointer<port_o>(port_i)->find_bounded_object();
      if(connectedPort == nullptr)
      {
         std::cout << "Found i var" << std::endl;
         SM->add_connection(request_datapath, port_i);
      }
   }
   PRINT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, " - Connected request");
}

void datapath_parallel_cs::instantiate_component_parallel(structural_objectRef clock_port, structural_objectRef reset_port)
{
   const structural_managerRef SM = HLS->datapath;
   const structural_objectRef circuit = SM->get_circ();
   PRINT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "Start to instantiate memory_ctrl_parallel");
   structural_type_descriptorRef bool_type = structural_type_descriptorRef(new structural_type_descriptor("bool", 0));
   std::string mem_par_model = "memory_ctrl_parallel";
   std::string mem_par_name = "memory_parallel";
   std::string mem_par_library = HLS->HLS_T->get_technology_manager()->get_library(mem_par_model);
   structural_objectRef mem_par_mod = SM->add_module_from_technology_library(mem_par_name, mem_par_model, mem_par_library, circuit, HLS->HLS_T->get_technology_manager());

   structural_objectRef clock_mem_par = mem_par_mod->find_member(CLOCK_PORT_NAME, port_o_K, mem_par_mod);
   structural_objectRef clock_sign = SM->add_sign("clock_mem_par_signal", circuit, bool_type);
   SM->add_connection(clock_sign, clock_port);
   SM->add_connection(clock_sign, clock_mem_par);

   structural_objectRef reset_mem_par = mem_par_mod->find_member(RESET_PORT_NAME, port_o_K, mem_par_mod);
   structural_objectRef reset_sign = SM->add_sign("reset_mem_par_signal", circuit, bool_type);
   SM->add_connection(reset_sign, reset_port);
   SM->add_connection(reset_sign, reset_mem_par);
   PRINT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "Instantiated memory_ctrl_parallel!");

   PRINT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "Starting setting parameter memory_ctrl_parallel!");
   GetPointer<module>(mem_par_mod)->SetParameter("NUM_CHANNEL", STR(parameters->getOption<unsigned int>(OPT_channels_number)));
   GetPointer<module>(mem_par_mod)->SetParameter("NUM_ACC", STR(parameters->getOption<unsigned int>(OPT_num_accelerators)));
   int addr_task = ceil_log2(parameters->getOption<unsigned long long int>(OPT_context_switch));
   if(!addr_task)
   {
      addr_task = 1;
   }
   GetPointer<module>(mem_par_mod)->SetParameter("ADDR_TASKS", STR(addr_task));
   int addr_kern = ceil_log2(parameters->getOption<unsigned long long>(OPT_num_accelerators));
   if(!addr_kern)
   {
      addr_kern = 1;
   }
   GetPointer<module>(mem_par_mod)->SetParameter("ADDR_ACC", STR(addr_kern));
   PRINT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "Parameter memory_ctrl_top set!");

   resize_ctrl_parallel_ports(mem_par_mod);
}

void datapath_parallel_cs::resize_ctrl_parallel_ports(structural_objectRef mem_par_mod)
{
   auto memory_channel = parameters->getOption<unsigned int>(OPT_channels_number);
   auto num_kernel = parameters->getOption<unsigned int>(OPT_num_accelerators);
   for(unsigned int j = 0; j < GetPointer<module>(mem_par_mod)->get_in_port_size(); j++) // resize input port
   {
      structural_objectRef port_i = GetPointer<module>(mem_par_mod)->get_in_port(j);
      if(GetPointer<port_o>(port_i)->get_is_memory())
      {
         std::string port_name = GetPointer<port_o>(port_i)->get_id();
         if(port_name.substr(0, 3) == "IN_")
         {
            resize_dimension_bus_port(memory_channel, port_i);
         }
         else
         {
            resize_dimension_bus_port(num_kernel, port_i);
         }
      }
   }
   for(unsigned int j = 0; j < GetPointer<module>(mem_par_mod)->get_out_port_size(); j++) // resize output port
   {
      structural_objectRef port_i = GetPointer<module>(mem_par_mod)->get_out_port(j);
      if(GetPointer<port_o>(port_i)->get_is_memory())
      {
         std::string port_name = GetPointer<port_o>(port_i)->get_id();
         if(port_name.substr(0, 4) == "OUT_")
         {
            resize_dimension_bus_port(memory_channel, port_i);
         }
         else
         {
            resize_dimension_bus_port(num_kernel, port_i);
         }
      }
   }
}

void datapath_parallel_cs::resize_dimension_bus_port(unsigned int vector_size, structural_objectRef port)
{
   unsigned int bus_data_bitsize = HLSMgr->Rmem->get_bus_data_bitsize();
   unsigned int bus_addr_bitsize = HLSMgr->get_address_bitsize();
   unsigned int bus_size_bitsize = HLSMgr->Rmem->get_bus_size_bitsize();
   unsigned int bus_tag_bitsize = GetPointer<memory_cs>(HLSMgr->Rmem)->get_bus_tag_bitsize();

   if(GetPointer<port_o>(port)->get_is_data_bus())
   {
      port->type_resize(bus_data_bitsize);
   }
   else if(GetPointer<port_o>(port)->get_is_addr_bus())
   {
      port->type_resize(bus_addr_bitsize);
   }
   else if(GetPointer<port_o>(port)->get_is_size_bus())
   {
      port->type_resize(bus_size_bitsize);
   }
   else if(GetPointer<port_o>(port)->get_is_tag_bus())
   {
      port->type_resize(bus_tag_bitsize);
   }

   GetPointer<port_o>(port)->add_n_ports(vector_size, port);
}

void datapath_parallel_cs::manage_extern_global_port_parallel(const structural_managerRef SM, const CustomOrderedSet<structural_objectRef>& memory_modules, const structural_objectRef circuit)
{
   structural_objectRef cir_port;
   structural_objectRef mem_paral_port;
   structural_objectRef memory_parallel = circuit->find_member("memory_parallel", component_o_K, circuit);
   unsigned int num_kernel = 0;
   INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "-->Connecting memory_port of memory_parallel");
   for(const auto& memory_module : memory_modules)
   {
      for(unsigned int j = 0; j < GetPointer<module>(memory_module)->get_in_port_size(); j++) // from ctrl_parallel to module
      {
         structural_objectRef port_i = GetPointer<module>(memory_module)->get_in_port(j);
         if(GetPointer<port_o>(port_i)->get_is_memory() && GetPointer<port_o>(port_i)->get_is_global() && GetPointer<port_o>(port_i)->get_is_extern())
         {
            std::string port_name = GetPointer<port_o>(port_i)->get_id();
            mem_paral_port = memory_parallel->find_member(port_name, port_vector_o_K, memory_parallel);
            structural_objectRef mem_paral_Sign = SM->add_sign(port_name + "_signal_" + STR(num_kernel), circuit, port_i->get_typeRef());
            THROW_ASSERT(!mem_paral_port || GetPointer<port_o>(mem_paral_port), "should be a port");
            SM->add_connection(GetPointer<port_o>(mem_paral_port)->get_port(num_kernel), mem_paral_Sign);
            SM->add_connection(mem_paral_Sign, port_i);
         }
      }
      for(unsigned int j = 0; j < GetPointer<module>(memory_module)->get_out_port_size(); j++) // from module to ctrl_parallel
      {
         structural_objectRef port_i = GetPointer<module>(memory_module)->get_out_port(j);
         if(GetPointer<port_o>(port_i)->get_is_memory() && !GetPointer<port_o>(port_i)->get_is_global() && !GetPointer<port_o>(port_i)->get_is_extern())
         {
            std::string port_name = GetPointer<port_o>(port_i)->get_id();
            mem_paral_port = memory_parallel->find_member(port_name, port_vector_o_K, memory_parallel);
            structural_objectRef mem_paral_Sign = SM->add_sign(port_name + "_signal_" + STR(num_kernel), circuit, port_i->get_typeRef());
            THROW_ASSERT(!mem_paral_port || GetPointer<port_o>(mem_paral_port), "should be a port");
            SM->add_connection(port_i, mem_paral_Sign);
            SM->add_connection(mem_paral_Sign, GetPointer<port_o>(mem_paral_port)->get_port(num_kernel));
         }
      }
      ++num_kernel;
   }

   for(unsigned int j = 0; j < GetPointer<module>(memory_parallel)->get_in_port_size(); j++) // connect input ctrl_parallel with input datapath
   {
      structural_objectRef port_i = GetPointer<module>(memory_parallel)->get_in_port(j);
      std::string port_name = GetPointer<port_o>(port_i)->get_id();
      if(GetPointer<port_o>(port_i)->get_is_memory() && GetPointer<port_o>(port_i)->get_is_global() && GetPointer<port_o>(port_i)->get_is_extern() && port_name.substr(0, 3) == "IN_")
      {
         cir_port = circuit->find_member(port_name.erase(0, 3), port_i->get_kind(), circuit);
         THROW_ASSERT(!cir_port || GetPointer<port_o>(cir_port), "should be a port or null");
         if(!cir_port)
         {
            cir_port = SM->add_port_vector(port_name, port_o::IN, GetPointer<port_o>(port_i)->get_ports_size(), circuit, port_i->get_typeRef());
            port_o::fix_port_properties(port_i, cir_port);
            SM->add_connection(cir_port, port_i);
         }
         else
         {
            SM->add_connection(cir_port, port_i);
         }
      }
   }
   for(unsigned int j = 0; j < GetPointer<module>(memory_parallel)->get_out_port_size(); j++) // connect output ctrl_parallel with output datapath
   {
      structural_objectRef port_i = GetPointer<module>(memory_parallel)->get_out_port(j);
      std::string port_name = GetPointer<port_o>(port_i)->get_id();
      if(GetPointer<port_o>(port_i)->get_is_memory() && !GetPointer<port_o>(port_i)->get_is_global() && !GetPointer<port_o>(port_i)->get_is_extern() && port_name.substr(0, 4) == "OUT_")
      {
         cir_port = circuit->find_member(port_name.erase(0, 4), port_i->get_kind(), circuit); // delete OUT from port name
         THROW_ASSERT(!cir_port || GetPointer<port_o>(cir_port), "should be a port or null");
         if(!cir_port)
         {
            cir_port = SM->add_port_vector(port_name, port_o::OUT, GetPointer<port_o>(port_i)->get_ports_size(), circuit, port_i->get_typeRef());
            port_o::fix_port_properties(port_i, cir_port);
            SM->add_connection(cir_port, port_i);
         }
         else
         {
            SM->add_connection(cir_port, port_i);
         }
      }
   }
   INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "<--Connected memory_port of memory_parallel");
}
