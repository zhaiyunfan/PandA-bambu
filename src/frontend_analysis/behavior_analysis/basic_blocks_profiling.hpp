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
 *              Copyright (C) 2015-2023 Politecnico di Milano
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
 * @file basic_blocks_profiling.hpp
 * @brief Analysis step performing a dynamic profiling of basic blocks execution
 *
 * @author Marco Lattuada <marco.lattuada@polimi.it>
 *
 */
#ifndef BASIC_BLOCKS_PROFILING_HPP
#define BASIC_BLOCKS_PROFILING_HPP

#include "application_frontend_flow_step.hpp"

/**
 * Class to perform profiling
 */
class BasicBlocksProfiling : public ApplicationFrontendFlowStep
{
   /// The instrumented source code
   const std::string profiling_source_file;

   const CustomUnorderedSet<std::pair<FrontendFlowStepType, FunctionRelationship>>
   ComputeFrontendRelationships(const DesignFlowStep::RelationshipType relationship_type) const override;

   void ComputeRelationships(DesignFlowStepSet& relationship,
                             const DesignFlowStep::RelationshipType relationship_type) final;

 public:
   /**
    * Constructor.
    * @param AppM is the application manager
    * @param design_flow_manager is the design flow manager
    * @param parameters is the set of the parameters
    */
   BasicBlocksProfiling(const application_managerRef AppM, const DesignFlowManagerConstRef design_flow_manager,
                        const ParameterConstRef parameters);

   void Initialize() final;

   DesignFlowStep_Status Exec() final;
};
#endif
