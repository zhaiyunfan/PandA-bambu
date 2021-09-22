/*
 *                 _/_/_/    _/_/   _/    _/ _/_/_/    _/_/
 *                _/   _/ _/    _/ _/_/  _/ _/   _/ _/    _/
 *               _/_/_/  _/_/_/_/ _/  _/_/ _/   _/ _/_/_/_/
 *              _/      _/    _/ _/    _/ _/   _/ _/    _/
 *             _/      _/    _/ _/    _/ _/_/_/  _/    _/
 *
 *           ***********************************************
 *                            PandA Project
 *                   URL: http://panda.dei.polimi.it
 *                     Politecnico di Milano - DEIB
 *                      System Architectures Group
 *           ***********************************************
 *            Copyright (C) 2004-2021 Politecnico di Milano
 *
 * This file is part of the PandA framework.
 *
 * The PandA framework is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @author Marco Minutoli <mminutoli@gmail.com>
 * @file
 * @brief Implementation of WB4Intercon_interface.
 */

#include "WB4Intercon_interface.hpp"

#include "Parameter.hpp"
#include "behavioral_helper.hpp"
#include "call_graph_manager.hpp"
#include "copyrights_strings.hpp"
#include "fileIO.hpp"
#include "hls.hpp"
#include "hls_manager.hpp"
#include "hls_target.hpp"
#include "memory.hpp"
#include "memory_symbol.hpp"
#include "structural_manager.hpp"
#include "structural_objects.hpp"
#include "technology_manager.hpp"
#include "technology_wishbone.hpp"
#include "tree_helper.hpp"
#include "tree_manager.hpp"

WB4Intercon_interface::WB4Intercon_interface(const ParameterConstRef P, const HLS_managerRef HLSManager, unsigned int functionId, const DesignFlowManagerConstRef _design_flow_manager)
    : WB4_interface(P, HLSManager, functionId, _design_flow_manager, HLSFlowStep_Type::WB4_INTERCON_INTERFACE_GENERATION)
{
}

WB4Intercon_interface::~WB4Intercon_interface() = default;

static void build_bus_interface(structural_managerRef SM, const hlsRef HLS, const HLS_managerRef HLSMgr);
static void buildCircuit(structural_managerRef SM, structural_objectRef wrappedObj, structural_objectRef interfaceObj, const hlsRef HLS, const HLS_managerRef HLSMgr);

void WB4Intercon_interface::exec()
{
   WB4_interface::InternalExec();

   const structural_managerRef SM = HLS->top;

   structural_objectRef wrappedObj = SM->get_circ();
   std::string module_name = wrappedObj->get_id() + "_interconnected";

   structural_managerRef SM_wb4_interconnected = structural_managerRef(new structural_manager(parameters));
   structural_type_descriptorRef module_type = structural_type_descriptorRef(new structural_type_descriptor(module_name));
   SM_wb4_interconnected->set_top_info(module_name, module_type);
   structural_objectRef interfaceObj = SM_wb4_interconnected->get_circ();

   // add the core to the wrapper
   wrappedObj->set_owner(interfaceObj);
   wrappedObj->set_id(wrappedObj->get_id() + "_i0");

   GetPointer<module>(interfaceObj)->add_internal_object(wrappedObj);
   /// Set some descriptions and legal stuff
   GetPointer<module>(interfaceObj)->set_description("WB4 interface for top component: " + wrappedObj->get_typeRef()->id_type);
   GetPointer<module>(interfaceObj)->set_copyright(GENERATED_COPYRIGHT);
   GetPointer<module>(interfaceObj)->set_authors("Component automatically generated by bambu");
   GetPointer<module>(interfaceObj)->set_license(GENERATED_LICENSE);

   build_bus_interface(SM_wb4_interconnected, HLS, HLSMgr);

   buildCircuit(SM_wb4_interconnected, wrappedObj, interfaceObj, HLS, HLSMgr);

   memory::propagate_memory_parameters(interfaceObj, SM_wb4_interconnected);

   // Generation completed
   HLS->top = SM_wb4_interconnected;
}

static unsigned int get_data_bus_bitsize(const hlsRef HLS, const HLS_managerRef HLSMgr)
{
   const auto function_behavior = HLSMgr->CGetFunctionBehavior(HLS->functionId);
   const auto behavioral_helper = function_behavior->CGetBehavioralHelper();
   std::map<unsigned int, memory_symbolRef> parameters = HLSMgr->Rmem->get_function_parameters(HLS->functionId);

   unsigned int data_bus_bitsize = HLSMgr->Rmem->get_bus_data_bitsize();
   for(auto function_parameter : parameters)
   {
      if(function_parameter.first != HLS->functionId)
      {
         data_bus_bitsize = std::max(data_bus_bitsize, tree_helper::Size(HLSMgr->get_tree_manager()->CGetTreeReindex(function_parameter.first)));
      }
   }
   return data_bus_bitsize;
}

static unsigned int get_addr_bus_bitsize(const HLS_managerRef HLSMgr)
{
   unsigned int addr_bus_bitsize = HLSMgr->get_address_bitsize();
   unsigned long long int allocated_space = HLSMgr->Rmem->get_max_address();
   unsigned int parameter_addr_bit = 1;
   while(allocated_space >>= 1)
   {
      ++parameter_addr_bit;
   }

   return std::max(parameter_addr_bit, addr_bus_bitsize);
}

static void build_bus_interface(structural_managerRef SM, const hlsRef HLS, const HLS_managerRef HLSMgr)
{
   structural_objectRef interfaceObj = SM->get_circ();

   structural_type_descriptorRef b_type = structural_type_descriptorRef(new structural_type_descriptor("bool", 1));

   unsigned int data_bus_bitsize = get_data_bus_bitsize(HLS, HLSMgr);
   unsigned int addr_bus_bitsize = get_addr_bus_bitsize(HLSMgr);

   structural_type_descriptorRef sel_type = structural_type_descriptorRef(new structural_type_descriptor("bool", data_bus_bitsize / 8));
   structural_type_descriptorRef addr_type = structural_type_descriptorRef(new structural_type_descriptor("bool", addr_bus_bitsize));
   structural_type_descriptorRef data_type = structural_type_descriptorRef(new structural_type_descriptor("bool", data_bus_bitsize));

   // Parameters
   std::string functionName = tree_helper::name_function(HLSMgr->get_tree_manager(), HLS->functionId);
   memory::add_memory_parameter(SM, WB_BASE_ADDRESS "_" + functionName + "_interconnected", STR(0));

   // Common Inputs
   SM->add_port(CLOCK_PORT_NAME, port_o::IN, interfaceObj, b_type);
   SM->add_port(RESET_PORT_NAME, port_o::IN, interfaceObj, b_type);

   // Master Inputs
   SM->add_port(WB_DATIM_PORT_NAME, port_o::IN, interfaceObj, data_type);
   SM->add_port(WB_ACKIM_PORT_NAME, port_o::IN, interfaceObj, b_type);

   // Master Outputs
   SM->add_port(WB_CYCOM_PORT_NAME, port_o::OUT, interfaceObj, b_type);
   SM->add_port(WB_STBOM_PORT_NAME, port_o::OUT, interfaceObj, b_type);
   SM->add_port(WB_WEOM_PORT_NAME, port_o::OUT, interfaceObj, b_type);
   SM->add_port(WB_ADDROM_PORT_NAME, port_o::OUT, interfaceObj, addr_type);
   SM->add_port(WB_DATOM_PORT_NAME, port_o::OUT, interfaceObj, data_type);
   SM->add_port(WB_SELOM_PORT_NAME, port_o::OUT, interfaceObj, sel_type);

   // Slave Inputs
   SM->add_port(WB_CYCIS_PORT_NAME, port_o::IN, interfaceObj, b_type);
   SM->add_port(WB_STBIS_PORT_NAME, port_o::IN, interfaceObj, b_type);
   SM->add_port(WB_WEIS_PORT_NAME, port_o::IN, interfaceObj, b_type);
   SM->add_port(WB_ADDRIS_PORT_NAME, port_o::IN, interfaceObj, addr_type);
   SM->add_port(WB_DATIS_PORT_NAME, port_o::IN, interfaceObj, data_type);
   SM->add_port(WB_SELIS_PORT_NAME, port_o::IN, interfaceObj, sel_type);

   // Slave Outputs
   SM->add_port(WB_DATOS_PORT_NAME, port_o::OUT, interfaceObj, data_type);
   SM->add_port(WB_ACKOS_PORT_NAME, port_o::OUT, interfaceObj, b_type);
}

static void buildCircuit(structural_managerRef SM, structural_objectRef wrappedObj, structural_objectRef interfaceObj, const hlsRef HLS, const HLS_managerRef HLSMgr)
{
   // Clock and reset connection
   SM->add_connection(interfaceObj->find_member(CLOCK_PORT_NAME, port_o_K, interfaceObj), wrappedObj->find_member(CLOCK_PORT_NAME, port_o_K, wrappedObj));

   SM->add_connection(interfaceObj->find_member(RESET_PORT_NAME, port_o_K, interfaceObj), wrappedObj->find_member(RESET_PORT_NAME, port_o_K, wrappedObj));

   const tree_managerRef TM = HLSMgr->get_tree_manager();

   structural_objectRef interconnect = SM->add_module_from_technology_library("intercon", WB4_INTERCON, WBLIBRARY, interfaceObj, HLS->HLS_T->get_technology_manager());

   auto* interconModule = GetPointer<module>(interconnect);
   unsigned int interconPortsNumber = interconModule->get_num_ports();
   for(unsigned int i = 0; i < interconPortsNumber; ++i)
   {
      structural_objectRef port = interconModule->get_positional_port(i);
      std::string portId = port->get_id();
      if(portId != CLOCK_PORT_NAME && portId != RESET_PORT_NAME)
      {
         size_t position = 0;
         if((position = portId.find("_om")) != std::string::npos)
         {
            portId.replace(position, 3, "_is");
         }
         else if((position = portId.find("_im")) != std::string::npos)
         {
            portId.replace(position, 3, "_os");
         }
         else if((position = portId.find("_os")) != std::string::npos)
         {
            portId.replace(position, 3, "_im");
         }
         else if((position = portId.find("_is")) != std::string::npos)
         {
            portId.replace(position, 3, "_om");
         }

         structural_objectRef destPort = interfaceObj->find_member(portId, port_o_K, interfaceObj);
         auto* portPtr = GetPointer<port_o>(port);
         portPtr->set_type(destPort->get_typeRef());
         portPtr->add_n_ports(1, port);
         SM->add_connection(destPort, portPtr->get_port(portPtr->get_ports_size() - 1));
      }
   }

   // Clock and reset connection
   SM->add_connection(interfaceObj->find_member(CLOCK_PORT_NAME, port_o_K, interfaceObj), interconnect->find_member(CLOCK_PORT_NAME, port_o_K, interconnect));

   SM->add_connection(interfaceObj->find_member(RESET_PORT_NAME, port_o_K, interfaceObj), interconnect->find_member(RESET_PORT_NAME, port_o_K, interconnect));

   std::string topFunctionName = tree_helper::name_function(HLSMgr->get_tree_manager(), HLS->functionId);
   std::string topModuleBaseAddress = WB_BASE_ADDRESS "_" + topFunctionName + "_interconnected";

   std::vector<structural_objectRef> masters, slaves;
   slaves.push_back(wrappedObj);

   std::string baseAddressFileName = "intercon_" + STR(HLS->functionId) + ".mem";
   std::ofstream baseAddressFile(GetPath(baseAddressFileName));

   std::string topFunctionBaseAddress = STR(WB_BASE_ADDRESS) + "_" + topFunctionName;
   wrappedObj->SetParameter(topFunctionBaseAddress, topModuleBaseAddress + " + " + topFunctionBaseAddress);

   baseAddressFile << std::bitset<8 * sizeof(unsigned int)>(HLSMgr->Rmem->get_first_address(HLS->functionId)) << '\n' << std::bitset<8 * sizeof(unsigned int)>(HLSMgr->Rmem->get_last_address(HLS->functionId, HLSMgr)) << '\n';

   if(wrappedObj->find_member(WB_CYCOM_PORT_NAME, port_o_K, wrappedObj))
   {
      masters.push_back(wrappedObj);
   }

   const CustomOrderedSet<unsigned int> additionalTops = HLSMgr->CGetCallGraphManager()->GetAddressedFunctions();
   for(unsigned int itr : additionalTops)
   {
      std::string functionName = tree_helper::name_function(TM, itr);
      std::string moduleName = functionName + "_minimal_interface_wb4_interface";

      baseAddressFile << std::bitset<8 * sizeof(unsigned int)>(HLSMgr->Rmem->get_first_address(itr)) << '\n' << std::bitset<8 * sizeof(unsigned int)>(HLSMgr->Rmem->get_last_address(itr, HLSMgr)) << '\n';
      structural_objectRef additionalTop = SM->add_module_from_technology_library(functionName, moduleName, WORK_LIBRARY, interfaceObj, HLS->HLS_T->get_technology_manager());

      std::string acceleratorBaseAddress = STR(WB_BASE_ADDRESS) + "_" + functionName;
      additionalTop->SetParameter(acceleratorBaseAddress, topModuleBaseAddress + " + " + acceleratorBaseAddress);

      // Clock and reset connection
      SM->add_connection(interfaceObj->find_member(CLOCK_PORT_NAME, port_o_K, interfaceObj), additionalTop->find_member(CLOCK_PORT_NAME, port_o_K, additionalTop));

      SM->add_connection(interfaceObj->find_member(RESET_PORT_NAME, port_o_K, interfaceObj), additionalTop->find_member(RESET_PORT_NAME, port_o_K, additionalTop));

      slaves.push_back(additionalTop);
      if(additionalTop->find_member(WB_CYCOM_PORT_NAME, port_o_K, additionalTop))
      {
         masters.push_back(additionalTop);
      }
   }

   baseAddressFile.close();

   structural_type_descriptorRef b_type = structural_type_descriptorRef(new structural_type_descriptor("bool", 1));
   structural_objectRef irqPort = SM->add_port_vector(WB_IRQ_PORT_NAME, port_o::OUT, static_cast<unsigned int>(slaves.size()), interfaceObj, b_type);

   unsigned int idx = 0;
   for(auto itr = slaves.begin(), end = slaves.end(); itr != end; ++itr, ++idx)
   {
      // Input ports
      auto* currentModule = GetPointer<module>(*itr);
      unsigned int inPortsNumber = currentModule->get_in_port_size();
      for(unsigned int i = 0; i < inPortsNumber; ++i)
      {
         structural_objectRef port = currentModule->get_in_port(i);
         std::string portId = port->get_id();
         structural_objectRef destPort = interconnect->find_member(portId, port_o_K, interconnect);
         if(destPort && portId != CLOCK_PORT_NAME && portId != RESET_PORT_NAME)
         {
            structural_objectRef signal = SM->add_sign(currentModule->get_id() + portId, SM->get_circ(), port->get_typeRef());
            SM->add_connection(port, signal);

            auto* destPortPtr = GetPointer<port_o>(destPort);
            destPortPtr->set_type(port->get_typeRef());
            destPortPtr->add_n_ports(1, destPort);
            SM->add_connection(signal, destPortPtr->get_port(destPortPtr->get_ports_size() - 1));
         }
      }

      unsigned int outPortsNumber = currentModule->get_out_port_size();
      for(unsigned int i = 0; i < outPortsNumber; ++i)
      {
         structural_objectRef port = currentModule->get_out_port(i);
         std::string portId = port->get_id();
         structural_objectRef destPort = interconnect->find_member(portId, port_o_K, interconnect);
         if(destPort)
         {
            structural_objectRef signal = SM->add_sign(currentModule->get_id() + portId, SM->get_circ(), port->get_typeRef());
            SM->add_connection(port, signal);

            auto* destPortPtr = GetPointer<port_o>(destPort);
            destPortPtr->set_type(port->get_typeRef());
            destPortPtr->add_n_ports(1, destPort);
            SM->add_connection(signal, destPortPtr->get_port(destPortPtr->get_ports_size() - 1));
         }
      }

      structural_objectRef irqSignal = SM->add_sign(STR(WB_IRQ_PORT_NAME) + "_" + STR(idx) + "_int", SM->get_circ(), b_type);

      SM->add_connection((*itr)->find_member(WB_IRQ_PORT_NAME, port_o_K, *itr), irqSignal);
      SM->add_connection(GetPointer<port_o>(irqPort)->get_port(idx), irqSignal);
   }

   interconModule->SetParameter("MASTERS", STR(masters.size() + 1));
   interconModule->SetParameter("SLAVES", STR(slaves.size() + 1));
   interconModule->SetParameter("MEMORY_INIT_file", "\"\"" + baseAddressFileName + "\"\"");
}
