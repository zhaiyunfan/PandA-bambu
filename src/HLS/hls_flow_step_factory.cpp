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
 *              Copyright (C) 2004-2023 Politecnico di Milano
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
 * @file hls_flow_step_factory.cpp
 * @brief Factory for scheduling
 *
 * @author Marco Lattuada <lattuada@elet.polimi.it>
 *
 */

/// Autoheader include
#include "config_HAVE_ILP_BUILT.hpp"

///. include
#include "Parameter.hpp"

/// Header include
#include "hls_flow_step_factory.hpp"
#include "call_graph_manager.hpp"
#include "design_flow_step.hpp"
#include "TopEntityMemoryMapped.hpp"
#include "top_entity.hpp"
#include "top_entity_cs.hpp"
#include "top_entity_parallel_cs.hpp"
#include "classic_datapath.hpp"
#include "datapath_cs.hpp"
#include "datapath_parallel_cs.hpp"
#include "control_flow_checker.hpp"
#include "controller_cs.hpp"
#include "fsm_controller.hpp"
#include "pipeline_controller.hpp"

/// HLS backend include
#include "generate_hdl.hpp"
#include "generate_simulation_scripts.hpp"
#include "generate_synthesis_scripts.hpp"
#if HAVE_TASTE
#include "generate_taste_hdl_architecture.hpp"
#include "generate_taste_synthesis_script.hpp"
#endif
#include "mux_connection_binding.hpp"
#include "cdfc_module_binding.hpp"
#include "easy_module_binding.hpp"
#include "port_swapping.hpp"
#include "unique_binding.hpp"
#include "chordal_coloring_register.hpp"
#include "compatibility_based_register.hpp"
#include "unique_binding_register.hpp"
#include "vertex_coloring_register.hpp"
#include "weighted_clique_register.hpp"

/// HLS/binding/storage_value_insertion includes
#include "values_scheme.hpp"

/// HLS/chaining
#include "sched_based_chaining_computation.hpp"

/// HLS/evaluation
#include "dry_run_evaluation.hpp"
#include "evaluation.hpp"

/// HLS/evaluation/exact includes
#include "simulation_evaluation.hpp"
#include "synthesis_evaluation.hpp"

/// HLS/function_allocation
#include "fun_dominator_allocation.hpp"
#if HAVE_FROM_PRAGMA_BUILT
#include "omp_function_allocation.hpp"
#include "omp_function_allocation_CS.hpp"
#endif

/// HLS/hls_flow includes
#include "classical_synthesis_flow.hpp"
#include "dominator_allocation.hpp"
#include "initialize_hls.hpp"
#if HAVE_FROM_PRAGMA_BUILT
#include "omp_body_loop_synthesis_flow.hpp"
#endif
#if HAVE_FROM_PRAGMA_BUILT
#include "omp_for_wrapper_cs_synthesis_flow.hpp"
#endif
#include "standard_hls.hpp"
#include "virtual_hls.hpp"
#include "write_hls_summary.hpp"

/// HLS/hls_flow/synthesis includes
#include "hls_synthesis_flow.hpp"

#if HAVE_TASTE
/// HLS/interface
#include "taste_interface_generation.hpp"
#endif

#include "cs_interface.hpp"
#include "minimal_interface.hpp"

/// HLS/interface/WB4 includes
#include "WB4Intercon_interface.hpp"
#include "WB4_interface.hpp"

/// HLS/liveness includes
#include "FSM_NI_SSA_liveness.hpp"

/// HLS/memory includes
#include "memory.hpp"

/// HLS/memory
#include "mem_dominator_allocation.hpp"
#include "mem_dominator_allocation_cs.hpp"

/// HLS/module_allocation includes
#include "add_library.hpp"
#include "allocation.hpp"
#include "hls_function_bit_value.hpp"
#if HAVE_FROM_PRAGMA_BUILT
#include "omp_allocation.hpp"
#endif

/// HLS/scheduling include
#include "parametric_list_based.hpp"
#include "scheduling.hpp"
#if HAVE_ILP_BUILT
#include "sdc_scheduling.hpp"
#endif

/// HLS/simulation includes
#include "CTestbenchExecution.hpp"
#include "test_vector_parser.hpp"
#include "testbench_generation.hpp"

/// HLS/stg
#include "BB_based_stg.hpp"

#include "CallGraphUnfolding.hpp"
#if HAVE_VCD_BUILT
#include "HWDiscrepancyAnalysis.hpp"
#include "HWPathComputation.hpp"
#include "VcdSignalSelection.hpp"
#include "vcd_utility.hpp"
#endif

/// polixml
#include "xml_element.hpp"

/// STD include
#include <string>

/// STL includes
#include "custom_set.hpp"
#include <utility>

/// tree include
#include "tree_manager.hpp"

/// utility include
#include "dbgPrintHelper.hpp"      // for DEBUG_LEVEL_
#include "string_manipulation.hpp" // for GET_CLASS
#include "xml_helper.hpp"

HLSFlowStepFactory::HLSFlowStepFactory(const DesignFlowManagerConstRef _design_flow_manager,
                                       const HLS_managerRef _HLS_mgr, const ParameterConstRef _parameters)
    : DesignFlowStepFactory(_design_flow_manager, _parameters), HLS_mgr(_HLS_mgr)
{
   debug_level = parameters->get_class_debug_level(GET_CLASS(*this));
}

HLSFlowStepFactory::~HLSFlowStepFactory() = default;

const std::string HLSFlowStepFactory::GetPrefix() const
{
   return "HLS";
}

DesignFlowStepRef
HLSFlowStepFactory::CreateHLSFlowStep(const HLSFlowStep_Type type, const unsigned int funId,
                                      const HLSFlowStepSpecializationConstRef hls_flow_step_specialization) const
{
   INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level,
                  "-->Creating step " +
                      (funId ? HLSFunctionStep::ComputeSignature(type, hls_flow_step_specialization, funId) :
                               HLS_step::ComputeSignature(type, hls_flow_step_specialization)) +
                      " (" + HLS_step::EnumToName(type) + ")");
   DesignFlowStepRef design_flow_step = DesignFlowStepRef();
   switch(type)
   {
      case HLSFlowStep_Type::ADD_LIBRARY:
      {
         design_flow_step = DesignFlowStepRef(
             new add_library(parameters, HLS_mgr, funId, design_flow_manager.lock(), hls_flow_step_specialization));
         break;
      }
      case HLSFlowStep_Type::ALLOCATION:
      {
         design_flow_step = DesignFlowStepRef(new allocation(parameters, HLS_mgr, funId, design_flow_manager.lock()));
         break;
      }
      case HLSFlowStep_Type::BB_STG_CREATOR:
      {
         design_flow_step = DesignFlowStepRef(new BB_based_stg(parameters, HLS_mgr, funId, design_flow_manager.lock()));
         break;
      }
      case HLSFlowStep_Type::CALL_GRAPH_UNFOLDING:
      {
         design_flow_step = DesignFlowStepRef(new CallGraphUnfolding(parameters, HLS_mgr, design_flow_manager.lock()));
         break;
      }
      case HLSFlowStep_Type::CDFC_MODULE_BINDING:
      {
         design_flow_step = DesignFlowStepRef(new cdfc_module_binding(
             parameters, HLS_mgr, funId, design_flow_manager.lock(), hls_flow_step_specialization));
         break;
      }
      case HLSFlowStep_Type::CHORDAL_COLORING_REGISTER_BINDING:
      {
         design_flow_step =
             DesignFlowStepRef(new chordal_coloring_register(parameters, HLS_mgr, funId, design_flow_manager.lock()));
         break;
      }
      case HLSFlowStep_Type::CLASSIC_DATAPATH_CREATOR:
      {
         design_flow_step =
             DesignFlowStepRef(new classic_datapath(parameters, HLS_mgr, funId, design_flow_manager.lock()));
         break;
      }
      case HLSFlowStep_Type::DATAPATH_CS_CREATOR:
      {
         design_flow_step = DesignFlowStepRef(new datapath_cs(parameters, HLS_mgr, funId, design_flow_manager.lock(),
                                                              HLSFlowStep_Type::DATAPATH_CS_CREATOR));
         break;
      }
      case HLSFlowStep_Type::DATAPATH_CS_PARALLEL_CREATOR:
      {
         design_flow_step = DesignFlowStepRef(new datapath_parallel_cs(
             parameters, HLS_mgr, funId, design_flow_manager.lock(), HLSFlowStep_Type::DATAPATH_CS_PARALLEL_CREATOR));
         break;
      }
      case HLSFlowStep_Type::CLASSICAL_HLS_SYNTHESIS_FLOW:
      {
         design_flow_step =
             DesignFlowStepRef(new ClassicalHLSSynthesisFlow(parameters, HLS_mgr, design_flow_manager.lock()));
         break;
      }
      case HLSFlowStep_Type::COLORING_REGISTER_BINDING:
      {
         design_flow_step =
             DesignFlowStepRef(new vertex_coloring_register(parameters, HLS_mgr, funId, design_flow_manager.lock()));
         break;
      }
      case HLSFlowStep_Type::CONTROL_FLOW_CHECKER:
      {
         design_flow_step =
             DesignFlowStepRef(new ControlFlowChecker(parameters, HLS_mgr, funId, design_flow_manager.lock()));
         break;
      }
      case HLSFlowStep_Type::C_TESTBENCH_EXECUTION:
      {
         design_flow_step = DesignFlowStepRef(
             new CTestbenchExecution(parameters, HLS_mgr, design_flow_manager.lock(), hls_flow_step_specialization));
         break;
      }
      case HLSFlowStep_Type::DOMINATOR_ALLOCATION:
      {
         design_flow_step =
             DesignFlowStepRef(new dominator_allocation(parameters, HLS_mgr, design_flow_manager.lock()));
         break;
      }
      case HLSFlowStep_Type::DOMINATOR_FUNCTION_ALLOCATION:
      {
         design_flow_step =
             DesignFlowStepRef(new fun_dominator_allocation(parameters, HLS_mgr, design_flow_manager.lock()));
         break;
      }
      case HLSFlowStep_Type::DOMINATOR_MEMORY_ALLOCATION:
      {
         design_flow_step = DesignFlowStepRef(new mem_dominator_allocation(
             parameters, HLS_mgr, design_flow_manager.lock(), hls_flow_step_specialization));
         break;
      }
      case HLSFlowStep_Type::DOMINATOR_MEMORY_ALLOCATION_CS:
      {
         design_flow_step = DesignFlowStepRef(new mem_dominator_allocation_cs(
             parameters, HLS_mgr, design_flow_manager.lock(), hls_flow_step_specialization,
             HLSFlowStep_Type::DOMINATOR_MEMORY_ALLOCATION_CS));
         break;
      }
      case HLSFlowStep_Type::DRY_RUN_EVALUATION:
      {
         design_flow_step = DesignFlowStepRef(new DryRunEvaluation(parameters, HLS_mgr, design_flow_manager.lock()));
         break;
      }
      case HLSFlowStep_Type::EASY_MODULE_BINDING:
      {
         design_flow_step =
             DesignFlowStepRef(new easy_module_binding(parameters, HLS_mgr, funId, design_flow_manager.lock()));
         break;
      }
      case HLSFlowStep_Type::LIST_BASED_SCHEDULING:
      {
         design_flow_step = DesignFlowStepRef(new parametric_list_based(
             parameters, HLS_mgr, funId, design_flow_manager.lock(), hls_flow_step_specialization));
         break;
      }
      case HLSFlowStep_Type::EVALUATION:
      {
         design_flow_step = DesignFlowStepRef(new Evaluation(parameters, HLS_mgr, design_flow_manager.lock()));
         break;
      }
      case HLSFlowStep_Type::FSM_CONTROLLER_CREATOR:
      {
         design_flow_step =
             DesignFlowStepRef(new fsm_controller(parameters, HLS_mgr, funId, design_flow_manager.lock()));
         break;
      }
      case HLSFlowStep_Type::FSM_CS_CONTROLLER_CREATOR:
      {
         design_flow_step = DesignFlowStepRef(new controller_cs(parameters, HLS_mgr, funId, design_flow_manager.lock(),
                                                                HLSFlowStep_Type::FSM_CS_CONTROLLER_CREATOR));
         break;
      }
      case HLSFlowStep_Type::FSM_NI_SSA_LIVENESS:
      {
         design_flow_step =
             DesignFlowStepRef(new FSM_NI_SSA_liveness(parameters, HLS_mgr, funId, design_flow_manager.lock()));
         break;
      }
      case HLSFlowStep_Type::GENERATE_HDL:
      {
         design_flow_step = DesignFlowStepRef(new generate_hdl(parameters, HLS_mgr, design_flow_manager.lock()));
         break;
      }
#if HAVE_SIMULATION_WRAPPER_BUILT
      case HLSFlowStep_Type::GENERATE_SIMULATION_SCRIPT:
      {
         design_flow_step =
             DesignFlowStepRef(new GenerateSimulationScripts(parameters, HLS_mgr, design_flow_manager.lock()));
         break;
      }
#endif
      case HLSFlowStep_Type::GENERATE_SYNTHESIS_SCRIPT:
      {
         design_flow_step =
             DesignFlowStepRef(new GenerateSynthesisScripts(parameters, HLS_mgr, design_flow_manager.lock()));
         break;
      }
#if HAVE_TASTE
      case HLSFlowStep_Type::GENERATE_TASTE_HDL_ARCHITECTURE:
      {
         design_flow_step =
             DesignFlowStepRef(new GenerateTasteHDLArchitecture(parameters, HLS_mgr, design_flow_manager.lock()));
         break;
      }
      case HLSFlowStep_Type::GENERATE_TASTE_SYNTHESIS_SCRIPT:
      {
         design_flow_step =
             DesignFlowStepRef(new GenerateTasteSynthesisScript(parameters, HLS_mgr, design_flow_manager.lock()));
         break;
      }
#endif
      case HLSFlowStep_Type::HLS_FUNCTION_BIT_VALUE:
      {
         design_flow_step =
             DesignFlowStepRef(new HLSFunctionBitValue(parameters, HLS_mgr, funId, design_flow_manager.lock()));
         break;
      }
      case HLSFlowStep_Type::HLS_SYNTHESIS_FLOW:
      {
         design_flow_step =
             DesignFlowStepRef(new HLSSynthesisFlow(parameters, HLS_mgr, funId, design_flow_manager.lock()));
         break;
      }
      case HLSFlowStep_Type::HW_PATH_COMPUTATION:
      {
         design_flow_step = DesignFlowStepRef(new HWPathComputation(parameters, HLS_mgr, design_flow_manager.lock()));
         break;
      }
      case HLSFlowStep_Type::HW_DISCREPANCY_ANALYSIS:
      {
         design_flow_step =
             DesignFlowStepRef(new HWDiscrepancyAnalysis(parameters, HLS_mgr, design_flow_manager.lock()));
         break;
      }
      case HLSFlowStep_Type::INITIALIZE_HLS:
      {
         design_flow_step =
             DesignFlowStepRef(new InitializeHLS(parameters, HLS_mgr, funId, design_flow_manager.lock()));
         break;
      }
      case HLSFlowStep_Type::INTERFACE_CS_GENERATION:
      {
         design_flow_step = DesignFlowStepRef(new cs_interface(parameters, HLS_mgr, funId, design_flow_manager.lock()));
         break;
      }
      case HLSFlowStep_Type::MINIMAL_INTERFACE_GENERATION:
      {
         design_flow_step =
             DesignFlowStepRef(new minimal_interface(parameters, HLS_mgr, funId, design_flow_manager.lock()));
         break;
      }
      case HLSFlowStep_Type::INFERRED_INTERFACE_GENERATION:
      {
         design_flow_step =
             DesignFlowStepRef(new minimal_interface(parameters, HLS_mgr, funId, design_flow_manager.lock()));
         break;
      }
      case HLSFlowStep_Type::MUX_INTERCONNECTION_BINDING:
      {
         design_flow_step =
             DesignFlowStepRef(new mux_connection_binding(parameters, HLS_mgr, funId, design_flow_manager.lock()));
         break;
      }
#if HAVE_FROM_PRAGMA_BUILT
      case HLSFlowStep_Type::OMP_ALLOCATION:
      {
         design_flow_step =
             DesignFlowStepRef(new OmpAllocation(parameters, HLS_mgr, funId, design_flow_manager.lock()));
         break;
      }
#endif
#if HAVE_FROM_PRAGMA_BUILT
      case HLSFlowStep_Type::OMP_BODY_LOOP_SYNTHESIS_FLOW:
      {
         design_flow_step =
             DesignFlowStepRef(new OmpBodyLoopSynthesisFlow(parameters, HLS_mgr, funId, design_flow_manager.lock()));
         break;
      }
#endif
#if HAVE_FROM_PRAGMA_BUILT
      case HLSFlowStep_Type::OMP_FOR_WRAPPER_CS_SYNTHESIS_FLOW:
      {
         design_flow_step = DesignFlowStepRef(
             new OmpForWrapperCSSynthesisFlow(parameters, HLS_mgr, funId, design_flow_manager.lock()));
         break;
      }
#endif
#if HAVE_FROM_PRAGMA_BUILT
      case HLSFlowStep_Type::OMP_FUNCTION_ALLOCATION:
      {
         design_flow_step =
             DesignFlowStepRef(new OmpFunctionAllocation(parameters, HLS_mgr, design_flow_manager.lock()));
         break;
      }
#endif
#if HAVE_FROM_PRAGMA_BUILT
      case HLSFlowStep_Type::OMP_FUNCTION_ALLOCATION_CS:
      {
         design_flow_step =
             DesignFlowStepRef(new OmpFunctionAllocationCS(parameters, HLS_mgr, design_flow_manager.lock()));
         break;
      }
#endif
      case HLSFlowStep_Type::PIPELINE_CONTROLLER_CREATOR:
      {
         design_flow_step =
             DesignFlowStepRef(new pipeline_controller(parameters, HLS_mgr, funId, design_flow_manager.lock()));
         break;
      }
      case HLSFlowStep_Type::PORT_SWAPPING:
      {
         design_flow_step =
             DesignFlowStepRef(new port_swapping(parameters, HLS_mgr, funId, design_flow_manager.lock()));
         break;
      }
      case HLSFlowStep_Type::SCHED_CHAINING:
      {
         design_flow_step = DesignFlowStepRef(
             new sched_based_chaining_computation(parameters, HLS_mgr, funId, design_flow_manager.lock()));
         break;
      }
#if HAVE_ILP_BUILT
      case HLSFlowStep_Type::SDC_SCHEDULING:
      {
         design_flow_step = DesignFlowStepRef(
             new SDCScheduling(parameters, HLS_mgr, funId, design_flow_manager.lock(), hls_flow_step_specialization));
         break;
      }
#endif
      case HLSFlowStep_Type::SIMULATION_EVALUATION:
      {
         design_flow_step =
             DesignFlowStepRef(new SimulationEvaluation(parameters, HLS_mgr, design_flow_manager.lock()));
         break;
      }
      case HLSFlowStep_Type::STANDARD_HLS_FLOW:
      {
         design_flow_step = DesignFlowStepRef(new standard_hls(parameters, HLS_mgr, funId, design_flow_manager.lock()));
         break;
      }
#if HAVE_LIBRARY_CHARACTERIZATION_BUILT
      case HLSFlowStep_Type::SYNTHESIS_EVALUATION:
      {
         design_flow_step = DesignFlowStepRef(new SynthesisEvaluation(parameters, HLS_mgr, design_flow_manager.lock()));
         break;
      }
#endif
#if HAVE_TASTE
      case HLSFlowStep_Type::TASTE_INTERFACE_GENERATION:
      {
         design_flow_step =
             DesignFlowStepRef(new TasteInterfaceGeneration(parameters, HLS_mgr, funId, design_flow_manager.lock()));
         break;
      }
#endif
      case HLSFlowStep_Type::TESTBENCH_GENERATION:
      {
         design_flow_step = DesignFlowStepRef(new TestbenchGeneration(parameters, HLS_mgr, design_flow_manager.lock()));
         break;
      }
      case HLSFlowStep_Type::TEST_VECTOR_PARSER:
      {
         design_flow_step = DesignFlowStepRef(new TestVectorParser(parameters, HLS_mgr, design_flow_manager.lock()));
         break;
      }
      case HLSFlowStep_Type::TOP_ENTITY_CREATION:
      {
         design_flow_step = DesignFlowStepRef(new top_entity(parameters, HLS_mgr, funId, design_flow_manager.lock()));
         break;
      }

      case HLSFlowStep_Type::TOP_ENTITY_CS_CREATION:
      {
         design_flow_step = DesignFlowStepRef(new top_entity_cs(parameters, HLS_mgr, funId, design_flow_manager.lock(),
                                                                HLSFlowStep_Type::TOP_ENTITY_CS_CREATION));
         break;
      }

      case HLSFlowStep_Type::TOP_ENTITY_CS_PARALLEL_CREATION:
      {
         design_flow_step =
             DesignFlowStepRef(new top_entity_parallel_cs(parameters, HLS_mgr, funId, design_flow_manager.lock(),
                                                          HLSFlowStep_Type::TOP_ENTITY_CS_PARALLEL_CREATION));
         break;
      }
      case HLSFlowStep_Type::TOP_ENTITY_MEMORY_MAPPED_CREATION:
      {
         design_flow_step =
             DesignFlowStepRef(new TopEntityMemoryMapped(parameters, HLS_mgr, funId, design_flow_manager.lock()));
         break;
      }
      case HLSFlowStep_Type::UNIQUE_MODULE_BINDING:
      {
         design_flow_step =
             DesignFlowStepRef(new unique_binding(parameters, HLS_mgr, funId, design_flow_manager.lock()));
         break;
      }
      case HLSFlowStep_Type::UNIQUE_REGISTER_BINDING:
      {
         design_flow_step =
             DesignFlowStepRef(new unique_binding_register(parameters, HLS_mgr, funId, design_flow_manager.lock()));
         break;
      }
      case HLSFlowStep_Type::VALUES_SCHEME_STORAGE_VALUE_INSERTION:
      {
         design_flow_step =
             DesignFlowStepRef(new values_scheme(parameters, HLS_mgr, funId, design_flow_manager.lock()));
         break;
      }
#if HAVE_VCD_BUILT
      case HLSFlowStep_Type::VCD_SIGNAL_SELECTION:
      {
         design_flow_step = DesignFlowStepRef(new VcdSignalSelection(parameters, HLS_mgr, design_flow_manager.lock()));
         break;
      }
      case HLSFlowStep_Type::VCD_UTILITY:
      {
         design_flow_step = DesignFlowStepRef(new vcd_utility(parameters, HLS_mgr, design_flow_manager.lock()));
         break;
      }
#endif
      case HLSFlowStep_Type::VIRTUAL_DESIGN_FLOW:
      {
         design_flow_step = DesignFlowStepRef(new virtual_hls(parameters, HLS_mgr, funId, design_flow_manager.lock()));
         break;
      }
      case HLSFlowStep_Type::WB4_INTERFACE_GENERATION:
      {
         design_flow_step =
             DesignFlowStepRef(new WB4_interface(parameters, HLS_mgr, funId, design_flow_manager.lock()));
         break;
      }
      case HLSFlowStep_Type::WB4_INTERCON_INTERFACE_GENERATION:
      {
         design_flow_step =
             DesignFlowStepRef(new WB4Intercon_interface(parameters, HLS_mgr, funId, design_flow_manager.lock()));
         break;
      }
      case HLSFlowStep_Type::WEIGHTED_CLIQUE_REGISTER_BINDING:
      {
         design_flow_step = DesignFlowStepRef(new weighted_clique_register(
             parameters, HLS_mgr, funId, design_flow_manager.lock(), hls_flow_step_specialization));
         break;
      }
      case HLSFlowStep_Type::WRITE_HLS_SUMMARY:
      {
         design_flow_step = DesignFlowStepRef(new WriteHLSSummary(parameters, HLS_mgr, design_flow_manager.lock()));
         break;
      }
      case HLSFlowStep_Type::UNKNOWN:
      {
         THROW_UNREACHABLE("");
         break;
      }
      default:
         THROW_UNREACHABLE("HLSFlowStep algorithm not supported");
   }
   INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level, "<--Created step " + design_flow_step->GetName());
   return design_flow_step;
}

const DesignFlowStepSet HLSFlowStepFactory::CreateHLSFlowSteps(
    const CustomUnorderedSet<std::pair<HLSFlowStep_Type, HLSFlowStepSpecializationConstRef>>& hls_flow_steps) const
{
   const CallGraphManagerConstRef call_graph_manager = HLS_mgr->CGetCallGraphManager();
   DesignFlowStepSet ret;

   for(const auto& hls_flow_step : hls_flow_steps)
   {
      INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level,
                     "-->Creating step " + HLS_step::EnumToName(hls_flow_step.first));
      switch(hls_flow_step.first)
      {
         case HLSFlowStep_Type::DRY_RUN_EVALUATION:
         case HLSFlowStep_Type::EVALUATION:
         case HLSFlowStep_Type::GENERATE_HDL:
         case HLSFlowStep_Type::TEST_VECTOR_PARSER:
         case HLSFlowStep_Type::CALL_GRAPH_UNFOLDING:
         case HLSFlowStep_Type::CLASSICAL_HLS_SYNTHESIS_FLOW:
         case HLSFlowStep_Type::DOMINATOR_ALLOCATION:
         case HLSFlowStep_Type::DOMINATOR_MEMORY_ALLOCATION:
         case HLSFlowStep_Type::DOMINATOR_MEMORY_ALLOCATION_CS:
         case HLSFlowStep_Type::DOMINATOR_FUNCTION_ALLOCATION:
#if HAVE_SIMULATION_WRAPPER_BUILT
         case HLSFlowStep_Type::GENERATE_SIMULATION_SCRIPT:
#endif
         case HLSFlowStep_Type::GENERATE_SYNTHESIS_SCRIPT:
         case HLSFlowStep_Type::HLS_SYNTHESIS_FLOW:
         case HLSFlowStep_Type::HW_PATH_COMPUTATION:
         case HLSFlowStep_Type::HW_DISCREPANCY_ANALYSIS:
         case HLSFlowStep_Type::TESTBENCH_GENERATION:
#if HAVE_VCD_BUILT
         case HLSFlowStep_Type::VCD_SIGNAL_SELECTION:
         case HLSFlowStep_Type::VCD_UTILITY:
#endif
         case HLSFlowStep_Type::WRITE_HLS_SUMMARY:

         {
            ret.insert(CreateHLSFlowStep(hls_flow_step.first, 0, hls_flow_step.second));
            break;
         }
         case HLSFlowStep_Type::UNKNOWN:
         case HLSFlowStep_Type::ADD_LIBRARY:
         case HLSFlowStep_Type::ALLOCATION:
         case HLSFlowStep_Type::BB_STG_CREATOR:
         case HLSFlowStep_Type::HLS_FUNCTION_BIT_VALUE:
         case HLSFlowStep_Type::CDFC_MODULE_BINDING:
         case HLSFlowStep_Type::CHORDAL_COLORING_REGISTER_BINDING:
         case HLSFlowStep_Type::CLASSIC_DATAPATH_CREATOR:
         case HLSFlowStep_Type::COLORING_REGISTER_BINDING:
         case HLSFlowStep_Type::CONTROL_FLOW_CHECKER:
         case HLSFlowStep_Type::C_TESTBENCH_EXECUTION:
         case HLSFlowStep_Type::DATAPATH_CS_CREATOR:
         case HLSFlowStep_Type::DATAPATH_CS_PARALLEL_CREATOR:
         case HLSFlowStep_Type::EASY_MODULE_BINDING:
         case HLSFlowStep_Type::FSM_CONTROLLER_CREATOR:
         case HLSFlowStep_Type::FSM_CS_CONTROLLER_CREATOR:
         case HLSFlowStep_Type::FSM_NI_SSA_LIVENESS:
#if HAVE_TASTE
         case HLSFlowStep_Type::GENERATE_TASTE_HDL_ARCHITECTURE:
         case HLSFlowStep_Type::GENERATE_TASTE_SYNTHESIS_SCRIPT:
#endif
         case HLSFlowStep_Type::INFERRED_INTERFACE_GENERATION:
         case HLSFlowStep_Type::INITIALIZE_HLS:
         case HLSFlowStep_Type::INTERFACE_CS_GENERATION:
         case HLSFlowStep_Type::LIST_BASED_SCHEDULING:
         case HLSFlowStep_Type::MINIMAL_INTERFACE_GENERATION:
         case HLSFlowStep_Type::MUX_INTERCONNECTION_BINDING:
#if HAVE_FROM_PRAGMA_BUILT
         case HLSFlowStep_Type::OMP_ALLOCATION:
#endif
#if HAVE_FROM_PRAGMA_BUILT
         case HLSFlowStep_Type::OMP_BODY_LOOP_SYNTHESIS_FLOW:
#endif
#if HAVE_FROM_PRAGMA_BUILT
         case HLSFlowStep_Type::OMP_FOR_WRAPPER_CS_SYNTHESIS_FLOW:
#endif
#if HAVE_FROM_PRAGMA_BUILT
         case HLSFlowStep_Type::OMP_FUNCTION_ALLOCATION:
#endif
#if HAVE_FROM_PRAGMA_BUILT
         case HLSFlowStep_Type::OMP_FUNCTION_ALLOCATION_CS:
#endif
         case HLSFlowStep_Type::PIPELINE_CONTROLLER_CREATOR:
         case HLSFlowStep_Type::PORT_SWAPPING:
         case HLSFlowStep_Type::SCHED_CHAINING:
#if HAVE_ILP_BUILT
         case HLSFlowStep_Type::SDC_SCHEDULING:
#endif
#if HAVE_SIMULATION_WRAPPER_BUILT
         case HLSFlowStep_Type::SIMULATION_EVALUATION:
#endif
         case HLSFlowStep_Type::STANDARD_HLS_FLOW:
#if HAVE_LIBRARY_CHARACTERIZATION_BUILT
         case HLSFlowStep_Type::SYNTHESIS_EVALUATION:
#endif
#if HAVE_TASTE
         case HLSFlowStep_Type::TASTE_INTERFACE_GENERATION:
#endif
         case HLSFlowStep_Type::TOP_ENTITY_CREATION:
         case HLSFlowStep_Type::TOP_ENTITY_CS_CREATION:
         case HLSFlowStep_Type::TOP_ENTITY_CS_PARALLEL_CREATION:
         case HLSFlowStep_Type::TOP_ENTITY_MEMORY_MAPPED_CREATION:
         case HLSFlowStep_Type::UNIQUE_MODULE_BINDING:
         case HLSFlowStep_Type::UNIQUE_REGISTER_BINDING:
         case HLSFlowStep_Type::VALUES_SCHEME_STORAGE_VALUE_INSERTION:
         case HLSFlowStep_Type::VIRTUAL_DESIGN_FLOW:
         case HLSFlowStep_Type::WB4_INTERCON_INTERFACE_GENERATION:
         case HLSFlowStep_Type::WB4_INTERFACE_GENERATION:
         case HLSFlowStep_Type::WEIGHTED_CLIQUE_REGISTER_BINDING:
         default:
            THROW_UNREACHABLE("Step not expected: " + HLS_step::EnumToName(hls_flow_step.first));
      }
      INDENT_DBG_MEX(DEBUG_LEVEL_VERY_PEDANTIC, debug_level,
                     "<--Created step " + HLS_step::EnumToName(hls_flow_step.first));
   }
   return ret;
}

const DesignFlowStepSet HLSFlowStepFactory::CreateHLSFlowSteps(
    const std::pair<HLSFlowStep_Type, HLSFlowStepSpecializationConstRef>& hls_flow_step) const
{
   CustomUnorderedSet<std::pair<HLSFlowStep_Type, HLSFlowStepSpecializationConstRef>> hls_flow_steps;
   hls_flow_steps.insert(hls_flow_step);
   return CreateHLSFlowSteps(hls_flow_steps);
}

const DesignFlowStepSet
HLSFlowStepFactory::CreateHLSFlowSteps(const HLSFlowStep_Type type,
                                       const HLSFlowStepSpecializationConstRef hls_flow_step_specialization) const
{
   const std::pair<HLSFlowStep_Type, HLSFlowStepSpecializationConstRef> hls_flow_step(type,
                                                                                      hls_flow_step_specialization);
   return CreateHLSFlowSteps(hls_flow_step);
}

const DesignFlowStepRef HLSFlowStepFactory::CreateHLSFlowStep(
    const std::pair<HLSFlowStep_Type, HLSFlowStepSpecializationConstRef>& hls_flow_step) const
{
   return *CreateHLSFlowSteps(hls_flow_step).cbegin();
}

const DesignFlowStepRef
HLSFlowStepFactory::CreateHLSFlowStep(const HLSFlowStep_Type type,
                                      const HLSFlowStepSpecializationConstRef hls_flow_step_specialization) const
{
   return *CreateHLSFlowSteps(type, hls_flow_step_specialization).cbegin();
}
