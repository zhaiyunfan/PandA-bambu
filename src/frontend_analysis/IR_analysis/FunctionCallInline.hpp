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
 *              Copyright (C) 2021 Politecnico di Milano
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
 * @file FunctionCallInline.hpp
 *
 * @author Michele Fiorito <michele.fiorito@polimi.it>
 * $Revision$
 * $Date$
 * Last modified by $Author$
 *
 */

#ifndef FUNCTION_CALL_INLINE_HPP
#define FUNCTION_CALL_INLINE_HPP

#include "function_frontend_flow_step.hpp"

#include "custom_map.hpp"
#include "custom_set.hpp"

REF_FORWARD_DECL(application_manager);
CONSTREF_FORWARD_DECL(DesignFlowManager);
CONSTREF_FORWARD_DECL(Parameter);
CONSTREF_FORWARD_DECL(tree_node);

class FunctionCallInline : public FunctionFrontendFlowStep
{
 private:
   /**
    * Return the set of analyses in relationship with this design step
    * @param relationship_type is the type of relationship to be considered
    */
   const CustomUnorderedSet<std::pair<FrontendFlowStepType, FunctionRelationship>> ComputeFrontendRelationships(const DesignFlowStep::RelationshipType relationship_type) const override;

   /// Set of always inlined functions
   static CustomSet<unsigned int> always_inline;

   static CustomMap<unsigned int, CustomSet<unsigned int>> inline_call;

 public:
   /**
    * Constructor.
    * @param Param is the set of the parameters
    * @param AppM is the application manager
    * @param function_id is the identifier of the function
    * @param DesignFlowManagerConstRef is the design flow manager
    */
   FunctionCallInline(const ParameterConstRef Param, const application_managerRef AppM, unsigned int function_id, const DesignFlowManagerConstRef design_flow_manager);

   /**
    *  Destructor
    */
   ~FunctionCallInline() override;

   /**
    * Computes the operations CFG graph data structure.
    * @return the exit status of this step
    */
   DesignFlowStep_Status InternalExec() override;

   bool HasToBeExecuted() const override;

   /**
    * @brief Request inlining for given call statement
    *
    * @param call_stmt the call statement to inline
    * @param caller_id id of the function where the call_stmt is present
    */
   static void RequestInlineStmt(const tree_nodeConstRef& call_stmt, unsigned int caller_id);
};
#endif
