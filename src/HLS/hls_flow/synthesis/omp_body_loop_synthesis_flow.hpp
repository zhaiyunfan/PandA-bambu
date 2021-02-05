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
 *              Copyright (c) 2015-2021 Politecnico di Milano
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
 * @file omp_body_loop_synthesis_flow.hpp
 * @brief Definition of the class to create the structural description for body loop of openmp for
 *
 * @author Marco Lattuada <marco.lattuada@polimi.it>
 *
 */
#ifndef OMP_BODY_LOOP_SYNTHESIS_FLOW_HPP
#define OMP_BODY_LOOP_SYNTHESIS_FLOW_HPP

#include "hls_function_step.hpp"

class OmpBodyLoopSynthesisFlow : public HLSFunctionStep
{
 protected:
   /**
    * Return the set of analyses in relationship with this design step
    * @param relationship_type is the type of relationship to be considered
    */
   virtual const CustomUnorderedSet<std::tuple<HLSFlowStep_Type, HLSFlowStepSpecializationConstRef, HLSFlowStep_Relationship>> ComputeHLSRelationships(const DesignFlowStep::RelationshipType relationship_type) const;

 public:
   /**
    * Constructor.
    * @param design_flow_manager is the design flow manager
    */
   OmpBodyLoopSynthesisFlow(const ParameterConstRef _parameters, const HLS_managerRef HLSMgr, unsigned int funId, const DesignFlowManagerConstRef design_flow_manager);

   /**
    * Destructor
    */
   virtual ~OmpBodyLoopSynthesisFlow();

   /**
    * Execute the step
    * @return the exit status of this step
    */
   virtual DesignFlowStep_Status InternalExec();
};
#endif
