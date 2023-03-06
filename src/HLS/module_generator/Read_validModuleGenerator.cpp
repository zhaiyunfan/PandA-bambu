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
 * @file Read_validModuleGenerator.cpp
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

#include "Read_validModuleGenerator.hpp"

#include "language_writer.hpp"

enum in_port
{
   i_clock = 0,
   i_reset,
   i_start,
   i_in1,
   i_in3,
   i_vld,
   i_last
};

enum out_port
{
   o_done = 0,
   o_out1,
   o_last
};

Read_validModuleGenerator::Read_validModuleGenerator(const HLS_managerRef& _HLSMgr) : Registrar(_HLSMgr)
{
}

void Read_validModuleGenerator::InternalExec(std::ostream& out, const module* /* mod */, unsigned int /* function_id */,
                                             vertex /* op_v */, const HDLWriter_Language /* language */,
                                             const std::vector<ModuleGenerator::parameter>& /* _p */,
                                             const std::vector<ModuleGenerator::parameter>& _ports_in,
                                             const std::vector<ModuleGenerator::parameter>& _ports_out,
                                             const std::vector<ModuleGenerator::parameter>& /* _ports_inout */)
{
   THROW_ASSERT(_ports_in.size() >= i_last, "");
   THROW_ASSERT(_ports_out.size() >= o_last, "");
   out << "reg started 1INIT_ZERO_VALUE;\n";
   out << "reg started0 1INIT_ZERO_VALUE;\n";
   out << "reg validated 1INIT_ZERO_VALUE;\n";
   out << "reg validated0 1INIT_ZERO_VALUE;\n";
   out << "reg [BITSIZE_" << _ports_out[o_out1].name << "-1:0] " << _ports_out[o_out1].name << " ;\n";
   out << "reg " << _ports_out[o_done].name << " 1INIT_ZERO_VALUE;\n";
   out << "reg [" << _ports_in[i_in3].type_size << "-1:0] reg_" << _ports_in[i_in3].name << " 1INIT_ZERO_VALUE;\n\n";

   out << "always @(*)\n";
   out << "  started0 = (started | " << _ports_in[i_start].name << ") & !(validated | " << _ports_in[i_vld].name
       << ");\n";
   out << "always @(posedge clock 1RESET_EDGE)\n";
   out << "  if (1RESET_VALUE)\n";
   out << "    started <= 0;\n";
   out << "  else\n";
   out << "    started <= started0;\n\n";

   out << "always @(*)\n";
   out << "  validated0 <= (validated | " << _ports_in[i_vld].name << ") & !(started | " << _ports_in[i_start].name
       << ");\n\n";

   out << "always @(posedge clock 1RESET_EDGE)\n";
   out << "  if (1RESET_VALUE)\n";
   out << "    validated <= 0;\n";
   out << "  else\n";
   out << "    validated <= validated0;\n\n";

   out << "always @(posedge clock 1RESET_EDGE)\n";
   out << "  if (1RESET_VALUE)\n";
   out << "    reg_" << _ports_in[i_in3].name << " <= 0;\n";
   out << "  else if(" << _ports_in[i_vld].name << ")\n";
   out << "    reg_" << _ports_in[i_in3].name << " <= " << _ports_in[i_in3].name << ";\n\n";

   out << "always @(*)\n";
   out << "begin\n";
   out << "  " << _ports_out[o_out1].name << " = " << _ports_in[i_vld].name << " ? " << _ports_in[i_in3].name
       << " : reg_" << _ports_in[i_in3].name << ";\n";
   out << "end\n\n";

   out << "always @(*)\n";
   out << "begin\n";
   out << "  " << _ports_out[o_done].name << " = (" << _ports_in[i_start].name << " & " << _ports_in[i_vld].name
       << ") | (started & " << _ports_in[i_vld].name << ")  | (validated & " << _ports_in[i_start].name << ");\n";
   out << "end\n";
}