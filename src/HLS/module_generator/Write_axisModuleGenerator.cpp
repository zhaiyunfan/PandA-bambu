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
 * @file Write_axisModuleGenerator.cpp
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

#include "Write_axisModuleGenerator.hpp"

#include "language_writer.hpp"

enum in_port
{
   i_clock = 0,
   i_reset,
   i_start,
   i_in1,
   i_in2,
   i_in3,
   i_ready,
   i_last
};

enum out_port
{
   o_done = 0,
   o_out1,
   o_tdata,
   o_valid,
   o_last
};

Write_axisModuleGenerator::Write_axisModuleGenerator(const HLS_managerRef& _HLSMgr) : Registrar(_HLSMgr)
{
}

void Write_axisModuleGenerator::InternalExec(std::ostream& out, structural_objectRef /* mod */,
                                             unsigned int /* function_id */, vertex /* op_v */,
                                             const HDLWriter_Language /* language */,
                                             const std::vector<ModuleGenerator::parameter>& /* _p */,
                                             const std::vector<ModuleGenerator::parameter>& _ports_in,
                                             const std::vector<ModuleGenerator::parameter>& _ports_out,
                                             const std::vector<ModuleGenerator::parameter>& /* _ports_inout */)
{
   THROW_ASSERT(_ports_in.size() >= i_last, "");
   THROW_ASSERT(_ports_out.size() >= o_last, "");
   out << "reg started 1INIT_ZERO_VALUE;\n";
   out << "reg started_0 1INIT_ZERO_VALUE;\n";
   out << "reg " << _ports_out[o_done].name << "_0 1INIT_ZERO_VALUE;\n\n";

   out << "always @(*)\n";
   out << "  started_0 = (started | " << _ports_in[i_start].name << ") & !" << _ports_in[i_ready].name << ";\n";
   out << "always @(posedge clock 1RESET_EDGE)\n";
   out << "  if (1RESET_VALUE)\n";
   out << "    started <= 0;\n";
   out << "  else\n";
   out << "    started <= started_0;\n\n";

   out << "always @(*)\n"
       << "begin\n"
       << "  " << _ports_out[o_done].name << "_0 = (" << _ports_in[i_start].name << " & " << _ports_in[i_ready].name
       << ") | (started & " << _ports_in[i_ready].name << ") ;\n"
       << "end\n\n";
   out << "assign " << _ports_out[o_done].name << " = " << _ports_out[o_done].name << "_0;\n";
   out << "assign " << _ports_out[o_valid].name << " = " << _ports_in[i_start].name << " | started;\n";
   out << "assign " << _ports_out[o_out1].name << " = " << _ports_in[i_ready].name << ";\n";
   out << "assign " << _ports_out[o_tdata].name << " = " << _ports_in[i_in2].name << ";\n";
}