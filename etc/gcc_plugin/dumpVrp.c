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
* @file dumpVrp.c
* @brief
*
* @author Fabrizio Ferrandi <fabrizio.ferrandi@polimi.it>
*
*/
#include "gcc-plugin.h"

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "flags.h"
#include "tree.h"
#include "basic-block.h"
#include "tree-flow.h"
#include "tree-pass.h"
#include "intl.h"

#include "cfgloop.h"
#include "toplev.h"

#include "VRP_data.h"

#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 7)
#include "diagnostic-core.h"
#endif
#include "tree-dump.h"
#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 7)
#include "gimple-pretty-print.h"
#include "tree-pretty-print.h"
#endif

#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 8)
/* Return the insn_code for a FLOAT_EXPR.  */
extern int can_float_p (int, int, int);
#define CODE_FOR_nothing 0
#endif

extern void scev_initialize(void);
extern void scev_finalize (void);


/******************************* tree-ssa-propagate.h ****************************/
/* If SIM_P is true, statement S will be simulated again.  */

static inline void
prop_set_simulate_again (GIMPLE_type s, bool visit_p)
{
  gimple_set_visited (s, visit_p);
}

/* Return true if statement T should be simulated again.  */

static inline bool
prop_simulate_again_p (GIMPLE_type s)
{
  return gimple_visited_p (s);
}

/* Lattice values used for propagation purposes.  Specific instances
   of a propagation engine must return these values from the statement
   and PHI visit functions to direct the engine.  */
enum ssa_prop_result {
    /* The statement produces nothing of interest.  No edges will be
       added to the work lists.  */
    SSA_PROP_NOT_INTERESTING,

    /* The statement produces an interesting value.  The set SSA_NAMEs
       returned by SSA_PROP_VISIT_STMT should be added to
       INTERESTING_SSA_EDGES.  If the statement being visited is a
       conditional jump, SSA_PROP_VISIT_STMT should indicate which edge
       out of the basic block should be marked executable.  */
    SSA_PROP_INTERESTING,

    /* The statement produces a varying (i.e., useless) value and
       should not be simulated again.  If the statement being visited
       is a conditional jump, all the edges coming out of the block
       will be considered executable.  */
    SSA_PROP_VARYING
};

#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 7)
typedef enum ssa_prop_result (*ssa_prop_visit_stmt_fn) (GIMPLE_type, edge *, tree *);
typedef enum ssa_prop_result (*ssa_prop_visit_phi_fn) (GIMPLE_type);
extern void ssa_propagate (ssa_prop_visit_stmt_fn, ssa_prop_visit_phi_fn);
#endif


/*********************************************************************************/

/******************************* tree-scalar-evolution.h *************************/
extern tree instantiate_scev (basic_block, struct loop *, tree);
extern void scev_analysis (void);
extern tree analyze_scalar_evolution (struct loop *, tree);

/* Returns the basic block preceding LOOP or ENTRY_BLOCK_PTR when the
   loop is function's body.  */

static inline basic_block
block_before_loop (loop_p loop)
{
  edge preheader = loop_preheader_edge (loop);
  return (preheader ? preheader->src : ENTRY_BLOCK_PTR);
}

/* Analyze all the parameters of the chrec that were left under a
   symbolic form.  LOOP is the loop in which symbolic names have to
   be analyzed and instantiated.  */

static inline tree
instantiate_parameters (struct loop *loop, tree chrec)
{
  return instantiate_scev (block_before_loop (loop), loop, chrec);
}

/* Returns the loop of the polynomial chrec CHREC.  */

static inline struct loop *
get_chrec_loop (const_tree chrec)
{
  return get_loop (CHREC_VARIABLE (chrec));
}

/*********************************************************************************/

/******************************* tree-chrec.h ************************************/
extern tree initial_condition_in_loop_num (tree, unsigned);
extern tree evolution_part_in_loop_num (tree, unsigned);

/*********************************************************************************/

#if (GCC_VERSION < 4006)
#ifdef ENABLE_CHECKING
#define gcc_checking_assert(EXPR) gcc_assert (EXPR)
#else
#define gcc_checking_assert(EXPR) ((void)(0 && (EXPR)))
#endif
/* In LTO -fwhole-program build we still want to keep the debug functions available
   for debugger.  Mark them as used to prevent removal.  */
#if (GCC_VERSION > 4000)
#define DEBUG_FUNCTION __attribute__ ((__used__))
#define DEBUG_VARIABLE __attribute__ ((__used__))
#else
#define DEBUG_FUNCTION
#define DEBUG_VARIABLE
#endif
#endif

#if (__GNUC__ > 4) || (__GNUC__ == 4 && __GNUC_MINOR__ >= 8)
#define VR_INITIALIZER { VR_UNDEFINED, NULL_TREE, NULL_TREE, NULL }
#endif


/* Set of SSA names found live during the RPO traversal of the function
   for still active basic-blocks.  */
static sbitmap *live;

/* Return true if the SSA name NAME is live on the edge E.  */

static bool
live_on_edge (edge e, tree name)
{
#if (__GNUC__ > 4) || (__GNUC__ == 4 && __GNUC_MINOR__ >= 8)
  return (live[e->dest->index]
	  && bitmap_bit_p (live[e->dest->index], SSA_NAME_VERSION (name)));
#else
  return (live[e->dest->index]
	  && TEST_BIT (live[e->dest->index], SSA_NAME_VERSION (name)));
#endif
}

/* Local functions.  */
static int compare_values (tree val1, tree val2);
static int compare_values_warnv (tree val1, tree val2, bool *);
static void vrp_meet (value_range_t *, value_range_t *);
#if (__GNUC__ > 4) || (__GNUC__ == 4 && __GNUC_MINOR__ >= 8)
static void vrp_intersect_ranges (value_range_t *, value_range_t *);
#endif
static tree vrp_evaluate_conditional_warnv_with_ops (enum tree_code,
						     tree, tree, bool, bool *,
						     bool *);



/* Location information for ASSERT_EXPRs.  Each instance of this
   structure describes an ASSERT_EXPR for an SSA name.  Since a single
   SSA name may have more than one assertion associated with it, these
   locations are kept in a linked list attached to the corresponding
   SSA name.  */
struct assert_locus_d
{
  /* Basic block where the assertion would be inserted.  */
  basic_block bb;

  /* Some assertions need to be inserted on an edge (e.g., assertions
     generated by COND_EXPRs).  In those cases, BB will be NULL.  */
  edge e;

  /* Pointer to the statement that generated this assertion.  */
  gimple_stmt_iterator si;

  /* Predicate code for the ASSERT_EXPR.  Must be COMPARISON_CLASS_P.  */
  enum tree_code comp_code;

  /* Value being compared against.  */
  tree val;

  /* Expression to compare.  */
  tree expr;

  /* Next node in the linked list.  */
  struct assert_locus_d *next;
};

typedef struct assert_locus_d *assert_locus_t;

/* If bit I is present, it means that SSA name N_i has a list of
   assertions that should be inserted in the IL.  */
static bitmap need_assert_for;

/* Array of locations lists where to insert assertions.  ASSERTS_FOR[I]
   holds a list of ASSERT_LOCUS_T nodes that describe where
   ASSERT_EXPRs for SSA name N_I should be inserted.  */
static assert_locus_t *asserts_for;

/* Value range array.  After propagation, VR_VALUE[I] holds the range
   of values that SSA name N_I may take.  */
static unsigned num_vr_values;
static value_range_t **vr_value;
static bool values_propagated;

/* For a PHI node which sets SSA name N_I, VR_COUNTS[I] holds the
   number of executable edges we saw the last time we visited the
   node.  */
static int *vr_phi_edge_counts;

#if (__GNUC__ > 4) || (__GNUC__ == 4 && __GNUC_MINOR__ >= 8)
typedef struct {
  GIMPLE_type stmt;
  tree vec;
} switch_update;

static vec<edge> to_remove_edges;
static vec<switch_update> to_update_switch_stmts;

extern void dump_value_range (FILE *, value_range_t *);
extern void dump_all_value_ranges (FILE *);
extern void debug_all_value_ranges (void);
extern void dump_all_asserts (FILE *);
#endif

#if (GCC_VERSION < 4006)

/* Return the maximum value for TYPE.  */

static inline tree
vrp_val_max (const_tree type)
{
  if (!INTEGRAL_TYPE_P (type))
    return NULL_TREE;

  return TYPE_MAX_VALUE (type);
}

/* Return the minimum value for TYPE.  */

static inline tree
vrp_val_min (const_tree type)
{
  if (!INTEGRAL_TYPE_P (type))
    return NULL_TREE;

  return TYPE_MIN_VALUE (type);
}

/* Return whether VAL is equal to the maximum value of its type.  This
   will be true for a positive overflow infinity.  We can't do a
   simple equality comparison with TYPE_MAX_VALUE because C typedefs
   and Ada subtypes can produce types whose TYPE_MAX_VALUE is not ==
   to the integer constant with the same value in the type.  */

static inline bool
vrp_val_is_max (const_tree val)
{
  tree type_max = vrp_val_max (TREE_TYPE (val));
  return (val == type_max
	  || (type_max != NULL_TREE
	      && operand_equal_p (val, type_max, 0)));
}

/* Return whether VAL is equal to the minimum value of its type.  This
   will be true for a negative overflow infinity.  */

static inline bool
vrp_val_is_min (const_tree val)
{
  tree type_min = vrp_val_min (TREE_TYPE (val));
  return (val == type_min
	  || (type_min != NULL_TREE
	      && operand_equal_p (val, type_min, 0)));
}


/* Return whether TYPE should use an overflow infinity distinct from
   TYPE_{MIN,MAX}_VALUE.  We use an overflow infinity value to
   represent a signed overflow during VRP computations.  An infinity
   is distinct from a half-range, which will go from some number to
   TYPE_{MIN,MAX}_VALUE.  */

static inline bool
needs_overflow_infinity (const_tree type)
{
  return INTEGRAL_TYPE_P (type) && !TYPE_OVERFLOW_WRAPS (type);
}

/* Return whether TYPE can support our overflow infinity
   representation: we use the TREE_OVERFLOW flag, which only exists
   for constants.  If TYPE doesn't support this, we don't optimize
   cases which would require signed overflow--we drop them to
   VARYING.  */

static inline bool
supports_overflow_infinity (const_tree type)
{
  tree min = vrp_val_min (type), max = vrp_val_max (type);
#ifdef ENABLE_CHECKING
  gcc_assert (needs_overflow_infinity (type));
#endif
  return (min != NULL_TREE
	  && CONSTANT_CLASS_P (min)
	  && max != NULL_TREE
	  && CONSTANT_CLASS_P (max));
}

/* VAL is the maximum or minimum value of a type.  Return a
   corresponding overflow infinity.  */

static inline tree
make_overflow_infinity (tree val)
{
#ifdef ENABLE_CHECKING
  gcc_assert (val != NULL_TREE && CONSTANT_CLASS_P (val));
#endif
  val = copy_node (val);
  TREE_OVERFLOW (val) = 1;
  return val;
}

/* Return a negative overflow infinity for TYPE.  */

static inline tree
negative_overflow_infinity (tree type)
{
#ifdef ENABLE_CHECKING
  gcc_assert (supports_overflow_infinity (type));
#endif
  return make_overflow_infinity (vrp_val_min (type));
}

/* Return a positive overflow infinity for TYPE.  */

static inline tree
positive_overflow_infinity (tree type)
{
#ifdef ENABLE_CHECKING
  gcc_assert (supports_overflow_infinity (type));
#endif
  return make_overflow_infinity (vrp_val_max (type));
}

/* Return whether VAL is a negative overflow infinity.  */

static inline bool
is_negative_overflow_infinity (const_tree val)
{
  return (needs_overflow_infinity (TREE_TYPE (val))
	  && CONSTANT_CLASS_P (val)
	  && TREE_OVERFLOW (val)
	  && vrp_val_is_min (val));
}

/* Return whether VAL is a positive overflow infinity.  */

static inline bool
is_positive_overflow_infinity (const_tree val)
{
  return (needs_overflow_infinity (TREE_TYPE (val))
	  && CONSTANT_CLASS_P (val)
	  && TREE_OVERFLOW (val)
	  && vrp_val_is_max (val));
}

/* Return whether VAL is a positive or negative overflow infinity.  */

static inline bool
is_overflow_infinity (const_tree val)
{
  return (needs_overflow_infinity (TREE_TYPE (val))
	  && CONSTANT_CLASS_P (val)
	  && TREE_OVERFLOW (val)
	  && (vrp_val_is_min (val) || vrp_val_is_max (val)));
}

/* Return whether STMT has a constant rhs that is_overflow_infinity. */

static inline bool
stmt_overflow_infinity (GIMPLE_type stmt)
{
  if (is_gimple_assign (stmt)
      && get_gimple_rhs_class (gimple_assign_rhs_code (stmt)) ==
      GIMPLE_SINGLE_RHS)
    return is_overflow_infinity (gimple_assign_rhs1 (stmt));
  return false;
}

/* If VAL is now an overflow infinity, return VAL.  Otherwise, return
   the same value with TREE_OVERFLOW clear.  This can be used to avoid
   confusing a regular value with an overflow value.  */

static inline tree
avoid_overflow_infinity (tree val)
{
  if (!is_overflow_infinity (val))
    return val;

  if (vrp_val_is_max (val))
    return vrp_val_max (TREE_TYPE (val));
  else
    {
#ifdef ENABLE_CHECKING
      gcc_assert (vrp_val_is_min (val));
#endif
      return vrp_val_min (TREE_TYPE (val));
    }
}


/* Return true if ARG is marked with the nonnull attribute in the
   current function signature.  */

static bool
nonnull_arg_p (const_tree arg)
{
  tree t, attrs, fntype;
  unsigned HOST_WIDE_INT arg_num;

  gcc_assert (TREE_CODE (arg) == PARM_DECL && POINTER_TYPE_P (TREE_TYPE (arg)));

  /* The static chain decl is always non null.  */
  if (arg == cfun->static_chain_decl)
    return true;

  fntype = TREE_TYPE (current_function_decl);
  attrs = lookup_attribute ("nonnull", TYPE_ATTRIBUTES (fntype));

  /* If "nonnull" wasn't specified, we know nothing about the argument.  */
  if (attrs == NULL_TREE)
    return false;

  /* If "nonnull" applies to all the arguments, then ARG is non-null.  */
  if (TREE_VALUE (attrs) == NULL_TREE)
    return true;

  /* Get the position number for ARG in the function signature.  */
  for (arg_num = 1, t = DECL_ARGUMENTS (current_function_decl);
       t;
       t = TREE_CHAIN (t), arg_num++)
    {
      if (t == arg)
	break;
    }

  gcc_assert (t == arg);

  /* Now see if ARG_NUM is mentioned in the nonnull list.  */
  for (t = TREE_VALUE (attrs); t; t = TREE_CHAIN (t))
    {
      if (compare_tree_int (TREE_VALUE (t), arg_num) == 0)
	return true;
    }

  return false;
}


/* Set value range VR to VR_VARYING.  */

static inline void
set_value_range_to_varying (value_range_t *vr)
{
  vr->type = VR_VARYING;
  vr->min = vr->max = NULL_TREE;
  if (vr->equiv)
    bitmap_clear (vr->equiv);
}


/* Set value range VR to {T, MIN, MAX, EQUIV}.  */

static void
set_value_range (value_range_t *vr, enum value_range_type t, tree min,
		 tree max, bitmap equiv)
{
#if defined ENABLE_CHECKING
  /* Check the validity of the range.  */
  if (t == VR_RANGE || t == VR_ANTI_RANGE)
    {
      int cmp;

      gcc_assert (min && max);

      if (INTEGRAL_TYPE_P (TREE_TYPE (min)) && t == VR_ANTI_RANGE)
	gcc_assert (!vrp_val_is_min (min) || !vrp_val_is_max (max));

      cmp = compare_values (min, max);
      gcc_assert (cmp == 0 || cmp == -1 || cmp == -2);

      if (needs_overflow_infinity (TREE_TYPE (min)))
	gcc_assert (!is_overflow_infinity (min)
		    || !is_overflow_infinity (max));
    }

  if (t == VR_UNDEFINED || t == VR_VARYING)
    gcc_assert (min == NULL_TREE && max == NULL_TREE);

  if (t == VR_UNDEFINED || t == VR_VARYING)
    gcc_assert (equiv == NULL || bitmap_empty_p (equiv));
#endif

  vr->type = t;
  vr->min = min;
  vr->max = max;

  /* Since updating the equivalence set involves deep copying the
     bitmaps, only do it if absolutely necessary.  */
  if (vr->equiv == NULL
      && equiv != NULL)
    vr->equiv = BITMAP_ALLOC (NULL);

  if (equiv != vr->equiv)
    {
      if (equiv && !bitmap_empty_p (equiv))
	bitmap_copy (vr->equiv, equiv);
      else
	bitmap_clear (vr->equiv);
    }
}


/* Set value range VR to the canonical form of {T, MIN, MAX, EQUIV}.
   This means adjusting T, MIN and MAX representing the case of a
   wrapping range with MAX < MIN covering [MIN, type_max] U [type_min, MAX]
   as anti-rage ~[MAX+1, MIN-1].  Likewise for wrapping anti-ranges.
   In corner cases where MAX+1 or MIN-1 wraps this will fall back
   to varying.
   This routine exists to ease canonicalization in the case where we
   extract ranges from var + CST op limit.  */

static void
set_and_canonicalize_value_range (value_range_t *vr, enum value_range_type t,
				  tree min, tree max, bitmap equiv)
{
  /* Nothing to canonicalize for symbolic or unknown or varying ranges.  */
  if ((t != VR_RANGE
       && t != VR_ANTI_RANGE)
      || TREE_CODE (min) != INTEGER_CST
      || TREE_CODE (max) != INTEGER_CST)
    {
      set_value_range (vr, t, min, max, equiv);
      return;
    }

  /* Wrong order for min and max, to swap them and the VR type we need
     to adjust them.  */
  if (tree_int_cst_lt (max, min))
    {
      tree one = build_int_cst (TREE_TYPE (min), 1);
      tree tmp = int_const_binop (PLUS_EXPR, max, one, 0);
      max = int_const_binop (MINUS_EXPR, min, one, 0);
      min = tmp;

      /* There's one corner case, if we had [C+1, C] before we now have
	 that again.  But this represents an empty value range, so drop
	 to varying in this case.  */
      if (tree_int_cst_lt (max, min))
	{
	  set_value_range_to_varying (vr);
	  return;
	}

      t = t == VR_RANGE ? VR_ANTI_RANGE : VR_RANGE;
    }

  /* Anti-ranges that can be represented as ranges should be so.  */
  if (t == VR_ANTI_RANGE)
    {
      bool is_min = vrp_val_is_min (min);
      bool is_max = vrp_val_is_max (max);

      if (is_min && is_max)
	{
	  /* We cannot deal with empty ranges, drop to varying.  */
	  set_value_range_to_varying (vr);
	  return;
	}
      else if (is_min
	       /* As a special exception preserve non-null ranges.  */
	       && !(TYPE_UNSIGNED (TREE_TYPE (min))
		    && integer_zerop (max)))
        {
	  tree one = build_int_cst (TREE_TYPE (max), 1);
	  min = int_const_binop (PLUS_EXPR, max, one, 0);
	  max = vrp_val_max (TREE_TYPE (max));
	  t = VR_RANGE;
        }
      else if (is_max)
        {
	  tree one = build_int_cst (TREE_TYPE (min), 1);
	  max = int_const_binop (MINUS_EXPR, min, one, 0);
	  min = vrp_val_min (TREE_TYPE (min));
	  t = VR_RANGE;
        }
    }

  set_value_range (vr, t, min, max, equiv);
}

/* Copy value range FROM into value range TO.  */

static inline void
copy_value_range (value_range_t *to, value_range_t *from)
{
  set_value_range (to, from->type, from->min, from->max, from->equiv);
}

/* Set value range VR to a single value.  This function is only called
   with values we get from statements, and exists to clear the
   TREE_OVERFLOW flag so that we don't think we have an overflow
   infinity when we shouldn't.  */

static inline void
set_value_range_to_value (value_range_t *vr, tree val, bitmap equiv)
{
  gcc_assert (is_gimple_min_invariant (val));
  val = avoid_overflow_infinity (val);
  set_value_range (vr, VR_RANGE, val, val, equiv);
}

/* Set value range VR to a non-negative range of type TYPE.
   OVERFLOW_INFINITY indicates whether to use an overflow infinity
   rather than TYPE_MAX_VALUE; this should be true if we determine
   that the range is nonnegative based on the assumption that signed
   overflow does not occur.  */

static inline void
set_value_range_to_nonnegative (value_range_t *vr, tree type,
				bool overflow_infinity)
{
  tree zero;

  if (overflow_infinity && !supports_overflow_infinity (type))
    {
      set_value_range_to_varying (vr);
      return;
    }

  zero = build_int_cst (type, 0);
  set_value_range (vr, VR_RANGE, zero,
		   (overflow_infinity
		    ? positive_overflow_infinity (type)
		    : TYPE_MAX_VALUE (type)),
		   vr->equiv);
}

/* Set value range VR to a non-NULL range of type TYPE.  */

static inline void
set_value_range_to_nonnull (value_range_t *vr, tree type)
{
  tree zero = build_int_cst (type, 0);
  set_value_range (vr, VR_ANTI_RANGE, zero, zero, vr->equiv);
}


/* Set value range VR to a NULL range of type TYPE.  */

static inline void
set_value_range_to_null (value_range_t *vr, tree type)
{
  set_value_range_to_value (vr, build_int_cst (type, 0), vr->equiv);
}


/* Set value range VR to a range of a truthvalue of type TYPE.  */

static inline void
set_value_range_to_truthvalue (value_range_t *vr, tree type)
{
  if (TYPE_PRECISION (type) == 1)
    set_value_range_to_varying (vr);
  else
    set_value_range (vr, VR_RANGE,
		     build_int_cst (type, 0), build_int_cst (type, 1),
		     vr->equiv);
}


/* Set value range VR to VR_UNDEFINED.  */

static inline void
set_value_range_to_undefined (value_range_t *vr)
{
  vr->type = VR_UNDEFINED;
  vr->min = vr->max = NULL_TREE;
  if (vr->equiv)
    bitmap_clear (vr->equiv);
}


/* If abs (min) < abs (max), set VR to [-max, max], if
   abs (min) >= abs (max), set VR to [-min, min].  */

static void
abs_extent_range (value_range_t *vr, tree min, tree max)
{
  int cmp;

  gcc_assert (TREE_CODE (min) == INTEGER_CST);
  gcc_assert (TREE_CODE (max) == INTEGER_CST);
  gcc_assert (INTEGRAL_TYPE_P (TREE_TYPE (min)));
  gcc_assert (!TYPE_UNSIGNED (TREE_TYPE (min)));
  min = fold_unary (ABS_EXPR, TREE_TYPE (min), min);
  max = fold_unary (ABS_EXPR, TREE_TYPE (max), max);
  if (TREE_OVERFLOW (min) || TREE_OVERFLOW (max))
    {
      set_value_range_to_varying (vr);
      return;
    }
  cmp = compare_values (min, max);
  if (cmp == -1)
    min = fold_unary (NEGATE_EXPR, TREE_TYPE (min), max);
  else if (cmp == 0 || cmp == 1)
    {
      max = min;
      min = fold_unary (NEGATE_EXPR, TREE_TYPE (min), min);
    }
  else
    {
      set_value_range_to_varying (vr);
      return;
    }
  set_and_canonicalize_value_range (vr, VR_RANGE, min, max, NULL);
}


/* Return value range information for VAR.

   If we have no values ranges recorded (ie, VRP is not running), then
   return NULL.  Otherwise create an empty range if none existed for VAR.  */

static value_range_t *
get_value_range (const_tree var)
{
  value_range_t *vr;
  tree sym;
  unsigned ver = SSA_NAME_VERSION (var);

  /* If we have no recorded ranges, then return NULL.  */
  if (! vr_value)
    return NULL;

  vr = vr_value[ver];
  if (vr)
    return vr;

  /* Create a default value range.  */
  vr_value[ver] = vr = XCNEW (value_range_t);

  /* Defer allocating the equivalence set.  */
  vr->equiv = NULL;

  /* If VAR is a default definition, the variable can take any value
     in VAR's type.  */
  sym = SSA_NAME_VAR (var);
  if (SSA_NAME_IS_DEFAULT_DEF (var))
    {
      /* Try to use the "nonnull" attribute to create ~[0, 0]
	 anti-ranges for pointers.  Note that this is only valid with
	 default definitions of PARM_DECLs.  */
      if (TREE_CODE (sym) == PARM_DECL
	  && POINTER_TYPE_P (TREE_TYPE (sym))
	  && nonnull_arg_p (sym))
	set_value_range_to_nonnull (vr, TREE_TYPE (sym));
      else
	set_value_range_to_varying (vr);
    }

  return vr;
}

/* Return true, if VAL1 and VAL2 are equal values for VRP purposes.  */

static inline bool
vrp_operand_equal_p (const_tree val1, const_tree val2)
{
  if (val1 == val2)
    return true;
  if (!val1 || !val2 || !operand_equal_p (val1, val2, 0))
    return false;
  if (is_overflow_infinity (val1))
    return is_overflow_infinity (val2);
  return true;
}

/* Return true, if the bitmaps B1 and B2 are equal.  */

static inline bool
vrp_bitmap_equal_p (const_bitmap b1, const_bitmap b2)
{
  return (b1 == b2
	  || (b1 && b2
	      && bitmap_equal_p (b1, b2)));
}

/* Update the value range and equivalence set for variable VAR to
   NEW_VR.  Return true if NEW_VR is different from VAR's previous
   value.

   NOTE: This function assumes that NEW_VR is a temporary value range
   object created for the sole purpose of updating VAR's range.  The
   storage used by the equivalence set from NEW_VR will be freed by
   this function.  Do not call update_value_range when NEW_VR
   is the range object associated with another SSA name.  */

static inline bool
update_value_range (const_tree var, value_range_t *new_vr)
{
  value_range_t *old_vr;
  bool is_new;

  /* Update the value range, if necessary.  */
  old_vr = get_value_range (var);
  is_new = old_vr->type != new_vr->type
	   || !vrp_operand_equal_p (old_vr->min, new_vr->min)
	   || !vrp_operand_equal_p (old_vr->max, new_vr->max)
	   || !vrp_bitmap_equal_p (old_vr->equiv, new_vr->equiv);

  if (is_new)
    set_value_range (old_vr, new_vr->type, new_vr->min, new_vr->max,
	             new_vr->equiv);

  BITMAP_FREE (new_vr->equiv);

  return is_new;
}


/* Add VAR and VAR's equivalence set to EQUIV.  This is the central
   point where equivalence processing can be turned on/off.  */

static void
add_equivalence (bitmap *equiv, const_tree var)
{
  unsigned ver = SSA_NAME_VERSION (var);
  value_range_t *vr = vr_value[ver];

  if (*equiv == NULL)
    *equiv = BITMAP_ALLOC (NULL);
  bitmap_set_bit (*equiv, ver);
  if (vr && vr->equiv)
    bitmap_ior_into (*equiv, vr->equiv);
}


/* Return true if VR is ~[0, 0].  */

static inline bool
range_is_nonnull (value_range_t *vr)
{
  return vr->type == VR_ANTI_RANGE
	 && integer_zerop (vr->min)
	 && integer_zerop (vr->max);
}


/* Return true if VR is [0, 0].  */

static inline bool
range_is_null (value_range_t *vr)
{
  return vr->type == VR_RANGE
	 && integer_zerop (vr->min)
	 && integer_zerop (vr->max);
}

/* Return true if max and min of VR are INTEGER_CST.  It's not necessary
   a singleton.  */

static inline bool
range_int_cst_p (value_range_t *vr)
{
  return (vr->type == VR_RANGE
	  && TREE_CODE (vr->max) == INTEGER_CST
	  && TREE_CODE (vr->min) == INTEGER_CST
	  && !TREE_OVERFLOW (vr->max)
	  && !TREE_OVERFLOW (vr->min));
}

/* Return true if VR is a INTEGER_CST singleton.  */

static inline bool
range_int_cst_singleton_p (value_range_t *vr)
{
  return (range_int_cst_p (vr)
	  && tree_int_cst_equal (vr->min, vr->max));
}

/* Return true if value range VR involves at least one symbol.  */

static inline bool
symbolic_range_p (value_range_t *vr)
{
  return (!is_gimple_min_invariant (vr->min)
          || !is_gimple_min_invariant (vr->max));
}

/* Return true if value range VR uses an overflow infinity.  */

static inline bool
overflow_infinity_range_p (value_range_t *vr)
{
  return (vr->type == VR_RANGE
	  && (is_overflow_infinity (vr->min)
	      || is_overflow_infinity (vr->max)));
}

/* Return false if we can not make a valid comparison based on VR;
   this will be the case if it uses an overflow infinity and overflow
   is not undefined (i.e., -fno-strict-overflow is in effect).
   Otherwise return true, and set *STRICT_OVERFLOW_P to true if VR
   uses an overflow infinity.  */

static bool
usable_range_p (value_range_t *vr, bool *strict_overflow_p)
{
  gcc_assert (vr->type == VR_RANGE);
  if (is_overflow_infinity (vr->min))
    {
      *strict_overflow_p = true;
      if (!TYPE_OVERFLOW_UNDEFINED (TREE_TYPE (vr->min)))
	return false;
    }
  if (is_overflow_infinity (vr->max))
    {
      *strict_overflow_p = true;
      if (!TYPE_OVERFLOW_UNDEFINED (TREE_TYPE (vr->max)))
	return false;
    }
  return true;
}


/* Like tree_expr_nonnegative_warnv_p, but this function uses value
   ranges obtained so far.  */

static bool
vrp_expr_computes_nonnegative (tree expr, bool *strict_overflow_p)
{
  return (tree_expr_nonnegative_warnv_p (expr, strict_overflow_p)
	  || (TREE_CODE (expr) == SSA_NAME
	      && ssa_name_nonnegative_p (expr)));
}

/* Return true if the result of assignment STMT is know to be non-negative.
   If the return value is based on the assumption that signed overflow is
   undefined, set *STRICT_OVERFLOW_P to true; otherwise, don't change
   *STRICT_OVERFLOW_P.*/

static bool
gimple_assign_nonnegative_warnv_p (GIMPLE_type stmt, bool *strict_overflow_p)
{
  enum tree_code code = gimple_assign_rhs_code (stmt);
  switch (get_gimple_rhs_class (code))
    {
    case GIMPLE_UNARY_RHS:
      return tree_unary_nonnegative_warnv_p (gimple_assign_rhs_code (stmt),
					     gimple_expr_type (stmt),
					     gimple_assign_rhs1 (stmt),
					     strict_overflow_p);
    case GIMPLE_BINARY_RHS:
      return tree_binary_nonnegative_warnv_p (gimple_assign_rhs_code (stmt),
					      gimple_expr_type (stmt),
					      gimple_assign_rhs1 (stmt),
					      gimple_assign_rhs2 (stmt),
					      strict_overflow_p);
    case GIMPLE_SINGLE_RHS:
      return tree_single_nonnegative_warnv_p (gimple_assign_rhs1 (stmt),
					      strict_overflow_p);
    case GIMPLE_INVALID_RHS:
      gcc_unreachable ();
    default:
      gcc_unreachable ();
    }
}

/* Return true if return value of call STMT is know to be non-negative.
   If the return value is based on the assumption that signed overflow is
   undefined, set *STRICT_OVERFLOW_P to true; otherwise, don't change
   *STRICT_OVERFLOW_P.*/

static bool
gimple_call_nonnegative_warnv_p (GIMPLE_type stmt, bool *strict_overflow_p)
{
  tree arg0 = gimple_call_num_args (stmt) > 0 ?
    gimple_call_arg (stmt, 0) : NULL_TREE;
  tree arg1 = gimple_call_num_args (stmt) > 1 ?
    gimple_call_arg (stmt, 1) : NULL_TREE;

  return tree_call_nonnegative_warnv_p (gimple_expr_type (stmt),
					gimple_call_fndecl (stmt),
					arg0,
					arg1,
					strict_overflow_p);
}

/* Return true if STMT is know to to compute a non-negative value.
   If the return value is based on the assumption that signed overflow is
   undefined, set *STRICT_OVERFLOW_P to true; otherwise, don't change
   *STRICT_OVERFLOW_P.*/

static bool
gimple_stmt_nonnegative_warnv_p (GIMPLE_type stmt, bool *strict_overflow_p)
{
  switch (gimple_code (stmt))
    {
    case GIMPLE_ASSIGN:
      return gimple_assign_nonnegative_warnv_p (stmt, strict_overflow_p);
    case GIMPLE_CALL:
      return gimple_call_nonnegative_warnv_p (stmt, strict_overflow_p);
    default:
      gcc_unreachable ();
    }
}

/* Return true if the result of assignment STMT is know to be non-zero.
   If the return value is based on the assumption that signed overflow is
   undefined, set *STRICT_OVERFLOW_P to true; otherwise, don't change
   *STRICT_OVERFLOW_P.*/

static bool
gimple_assign_nonzero_warnv_p (GIMPLE_type stmt, bool *strict_overflow_p)
{
  enum tree_code code = gimple_assign_rhs_code (stmt);
  switch (get_gimple_rhs_class (code))
    {
    case GIMPLE_UNARY_RHS:
      return tree_unary_nonzero_warnv_p (gimple_assign_rhs_code (stmt),
					 gimple_expr_type (stmt),
					 gimple_assign_rhs1 (stmt),
					 strict_overflow_p);
    case GIMPLE_BINARY_RHS:
      return tree_binary_nonzero_warnv_p (gimple_assign_rhs_code (stmt),
					  gimple_expr_type (stmt),
					  gimple_assign_rhs1 (stmt),
					  gimple_assign_rhs2 (stmt),
					  strict_overflow_p);
    case GIMPLE_SINGLE_RHS:
      return tree_single_nonzero_warnv_p (gimple_assign_rhs1 (stmt),
					  strict_overflow_p);
    case GIMPLE_INVALID_RHS:
      gcc_unreachable ();
    default:
      gcc_unreachable ();
    }
}

/* Return true if STMT is know to to compute a non-zero value.
   If the return value is based on the assumption that signed overflow is
   undefined, set *STRICT_OVERFLOW_P to true; otherwise, don't change
   *STRICT_OVERFLOW_P.*/

static bool
gimple_stmt_nonzero_warnv_p (GIMPLE_type stmt, bool *strict_overflow_p)
{
  switch (gimple_code (stmt))
    {
    case GIMPLE_ASSIGN:
      return gimple_assign_nonzero_warnv_p (stmt, strict_overflow_p);
    case GIMPLE_CALL:
      return gimple_alloca_call_p (stmt);
    default:
      gcc_unreachable ();
    }
}

/* Like tree_expr_nonzero_warnv_p, but this function uses value ranges
   obtained so far.  */

static bool
vrp_stmt_computes_nonzero (GIMPLE_type stmt, bool *strict_overflow_p)
{
  if (gimple_stmt_nonzero_warnv_p (stmt, strict_overflow_p))
    return true;

  /* If we have an expression of the form &X->a, then the expression
     is nonnull if X is nonnull.  */
  if (is_gimple_assign (stmt)
      && gimple_assign_rhs_code (stmt) == ADDR_EXPR)
    {
      tree expr = gimple_assign_rhs1 (stmt);
      tree base = get_base_address (TREE_OPERAND (expr, 0));

      if (base != NULL_TREE
	  && TREE_CODE (base) == INDIRECT_REF
	  && TREE_CODE (TREE_OPERAND (base, 0)) == SSA_NAME)
	{
	  value_range_t *vr = get_value_range (TREE_OPERAND (base, 0));
	  if (range_is_nonnull (vr))
	    return true;
	}
    }

  return false;
}

/* Returns true if EXPR is a valid value (as expected by compare_values) --
   a GIMPLE_type invariant, or SSA_NAME +- CST.  */

static bool
valid_value_p (tree expr)
{
  if (TREE_CODE (expr) == SSA_NAME)
    return true;

  if (TREE_CODE (expr) == PLUS_EXPR
      || TREE_CODE (expr) == MINUS_EXPR)
    return (TREE_CODE (TREE_OPERAND (expr, 0)) == SSA_NAME
	    && TREE_CODE (TREE_OPERAND (expr, 1)) == INTEGER_CST);

  return is_gimple_min_invariant (expr);
}

/* Return
   1 if VAL < VAL2
   0 if !(VAL < VAL2)
   -2 if those are incomparable.  */
static inline int
operand_less_p (tree val, tree val2)
{
  /* LT is folded faster than GE and others.  Inline the common case.  */
  if (TREE_CODE (val) == INTEGER_CST && TREE_CODE (val2) == INTEGER_CST)
    {
      if (TYPE_UNSIGNED (TREE_TYPE (val)))
	return INT_CST_LT_UNSIGNED (val, val2);
      else
	{
	  if (INT_CST_LT (val, val2))
	    return 1;
	}
    }
  else
    {
      tree tcmp;

      fold_defer_overflow_warnings ();

      tcmp = fold_binary_to_constant (LT_EXPR, boolean_type_node, val, val2);

      fold_undefer_and_ignore_overflow_warnings ();

      if (!tcmp
	  || TREE_CODE (tcmp) != INTEGER_CST)
	return -2;

      if (!integer_zerop (tcmp))
	return 1;
    }

  /* val >= val2, not considering overflow infinity.  */
  if (is_negative_overflow_infinity (val))
    return is_negative_overflow_infinity (val2) ? 0 : 1;
  else if (is_positive_overflow_infinity (val2))
    return is_positive_overflow_infinity (val) ? 0 : 1;

  return 0;
}

/* Compare two values VAL1 and VAL2.  Return

   	-2 if VAL1 and VAL2 cannot be compared at compile-time,
   	-1 if VAL1 < VAL2,
   	 0 if VAL1 == VAL2,
	+1 if VAL1 > VAL2, and
	+2 if VAL1 != VAL2

   This is similar to tree_int_cst_compare but supports pointer values
   and values that cannot be compared at compile time.

   If STRICT_OVERFLOW_P is not NULL, then set *STRICT_OVERFLOW_P to
   true if the return value is only valid if we assume that signed
   overflow is undefined.  */

static int
compare_values_warnv (tree val1, tree val2, bool *strict_overflow_p)
{
  if (val1 == val2)
    return 0;

  /* Below we rely on the fact that VAL1 and VAL2 are both pointers or
     both integers.  */
  gcc_assert (POINTER_TYPE_P (TREE_TYPE (val1))
	      == POINTER_TYPE_P (TREE_TYPE (val2)));
  /* Convert the two values into the same type.  This is needed because
     sizetype causes sign extension even for unsigned types.  */
  val2 = fold_convert (TREE_TYPE (val1), val2);
  STRIP_USELESS_TYPE_CONVERSION (val2);

  if ((TREE_CODE (val1) == SSA_NAME
       || TREE_CODE (val1) == PLUS_EXPR
       || TREE_CODE (val1) == MINUS_EXPR)
      && (TREE_CODE (val2) == SSA_NAME
	  || TREE_CODE (val2) == PLUS_EXPR
	  || TREE_CODE (val2) == MINUS_EXPR))
    {
      tree n1, c1, n2, c2;
      enum tree_code code1, code2;

      /* If VAL1 and VAL2 are of the form 'NAME [+-] CST' or 'NAME',
	 return -1 or +1 accordingly.  If VAL1 and VAL2 don't use the
	 same name, return -2.  */
      if (TREE_CODE (val1) == SSA_NAME)
	{
	  code1 = SSA_NAME;
	  n1 = val1;
	  c1 = NULL_TREE;
	}
      else
	{
	  code1 = TREE_CODE (val1);
	  n1 = TREE_OPERAND (val1, 0);
	  c1 = TREE_OPERAND (val1, 1);
	  if (tree_int_cst_sgn (c1) == -1)
	    {
	      if (is_negative_overflow_infinity (c1))
		return -2;
	      c1 = fold_unary_to_constant (NEGATE_EXPR, TREE_TYPE (c1), c1);
	      if (!c1)
		return -2;
	      code1 = code1 == MINUS_EXPR ? PLUS_EXPR : MINUS_EXPR;
	    }
	}

      if (TREE_CODE (val2) == SSA_NAME)
	{
	  code2 = SSA_NAME;
	  n2 = val2;
	  c2 = NULL_TREE;
	}
      else
	{
	  code2 = TREE_CODE (val2);
	  n2 = TREE_OPERAND (val2, 0);
	  c2 = TREE_OPERAND (val2, 1);
	  if (tree_int_cst_sgn (c2) == -1)
	    {
	      if (is_negative_overflow_infinity (c2))
		return -2;
	      c2 = fold_unary_to_constant (NEGATE_EXPR, TREE_TYPE (c2), c2);
	      if (!c2)
		return -2;
	      code2 = code2 == MINUS_EXPR ? PLUS_EXPR : MINUS_EXPR;
	    }
	}

      /* Both values must use the same name.  */
      if (n1 != n2)
	return -2;

      if (code1 == SSA_NAME
	  && code2 == SSA_NAME)
	/* NAME == NAME  */
	return 0;

      /* If overflow is defined we cannot simplify more.  */
      if (!TYPE_OVERFLOW_UNDEFINED (TREE_TYPE (val1)))
	return -2;

      if (strict_overflow_p != NULL
	  && (code1 == SSA_NAME || !TREE_NO_WARNING (val1))
	  && (code2 == SSA_NAME || !TREE_NO_WARNING (val2)))
	*strict_overflow_p = true;

      if (code1 == SSA_NAME)
	{
	  if (code2 == PLUS_EXPR)
	    /* NAME < NAME + CST  */
	    return -1;
	  else if (code2 == MINUS_EXPR)
	    /* NAME > NAME - CST  */
	    return 1;
	}
      else if (code1 == PLUS_EXPR)
	{
	  if (code2 == SSA_NAME)
	    /* NAME + CST > NAME  */
	    return 1;
	  else if (code2 == PLUS_EXPR)
	    /* NAME + CST1 > NAME + CST2, if CST1 > CST2  */
	    return compare_values_warnv (c1, c2, strict_overflow_p);
	  else if (code2 == MINUS_EXPR)
	    /* NAME + CST1 > NAME - CST2  */
	    return 1;
	}
      else if (code1 == MINUS_EXPR)
	{
	  if (code2 == SSA_NAME)
	    /* NAME - CST < NAME  */
	    return -1;
	  else if (code2 == PLUS_EXPR)
	    /* NAME - CST1 < NAME + CST2  */
	    return -1;
	  else if (code2 == MINUS_EXPR)
	    /* NAME - CST1 > NAME - CST2, if CST1 < CST2.  Notice that
	       C1 and C2 are swapped in the call to compare_values.  */
	    return compare_values_warnv (c2, c1, strict_overflow_p);
	}

      gcc_unreachable ();
    }

  /* We cannot compare non-constants.  */
  if (!is_gimple_min_invariant (val1) || !is_gimple_min_invariant (val2))
    return -2;

  if (!POINTER_TYPE_P (TREE_TYPE (val1)))
    {
      /* We cannot compare overflowed values, except for overflow
	 infinities.  */
      if (TREE_OVERFLOW (val1) || TREE_OVERFLOW (val2))
	{
	  if (strict_overflow_p != NULL)
	    *strict_overflow_p = true;
	  if (is_negative_overflow_infinity (val1))
	    return is_negative_overflow_infinity (val2) ? 0 : -1;
	  else if (is_negative_overflow_infinity (val2))
	    return 1;
	  else if (is_positive_overflow_infinity (val1))
	    return is_positive_overflow_infinity (val2) ? 0 : 1;
	  else if (is_positive_overflow_infinity (val2))
	    return -1;
	  return -2;
	}

      return tree_int_cst_compare (val1, val2);
    }
  else
    {
      tree t;

      /* First see if VAL1 and VAL2 are not the same.  */
      if (val1 == val2 || operand_equal_p (val1, val2, 0))
	return 0;

      /* If VAL1 is a lower address than VAL2, return -1.  */
      if (operand_less_p (val1, val2) == 1)
	return -1;

      /* If VAL1 is a higher address than VAL2, return +1.  */
      if (operand_less_p (val2, val1) == 1)
	return 1;

      /* If VAL1 is different than VAL2, return +2.
	 For integer constants we either have already returned -1 or 1
	 or they are equivalent.  We still might succeed in proving
	 something about non-trivial operands.  */
      if (TREE_CODE (val1) != INTEGER_CST
	  || TREE_CODE (val2) != INTEGER_CST)
	{
          t = fold_binary_to_constant (NE_EXPR, boolean_type_node, val1, val2);
	  if (t && integer_onep (t))
	    return 2;
	}

      return -2;
    }
}

/* Compare values like compare_values_warnv, but treat comparisons of
   nonconstants which rely on undefined overflow as incomparable.  */

static int
compare_values (tree val1, tree val2)
{
  bool sop;
  int ret;

  sop = false;
  ret = compare_values_warnv (val1, val2, &sop);
  if (sop
      && (!is_gimple_min_invariant (val1) || !is_gimple_min_invariant (val2)))
    ret = -2;
  return ret;
}


/* Return 1 if VAL is inside value range VR (VR->MIN <= VAL <= VR->MAX),
          0 if VAL is not inside VR,
	 -2 if we cannot tell either way.

   FIXME, the current semantics of this functions are a bit quirky
	  when taken in the context of VRP.  In here we do not care
	  about VR's type.  If VR is the anti-range ~[3, 5] the call
	  value_inside_range (4, VR) will return 1.

	  This is counter-intuitive in a strict sense, but the callers
	  currently expect this.  They are calling the function
	  merely to determine whether VR->MIN <= VAL <= VR->MAX.  The
	  callers are applying the VR_RANGE/VR_ANTI_RANGE semantics
	  themselves.

	  This also applies to value_ranges_intersect_p and
	  range_includes_zero_p.  The semantics of VR_RANGE and
	  VR_ANTI_RANGE should be encoded here, but that also means
	  adapting the users of these functions to the new semantics.

   Benchmark compile/20001226-1.c compilation time after changing this
   function.  */

static inline int
value_inside_range (tree val, value_range_t * vr)
{
  int cmp1, cmp2;

  cmp1 = operand_less_p (val, vr->min);
  if (cmp1 == -2)
    return -2;
  if (cmp1 == 1)
    return 0;

  cmp2 = operand_less_p (vr->max, val);
  if (cmp2 == -2)
    return -2;

  return !cmp2;
}


/* Return true if value ranges VR0 and VR1 have a non-empty
   intersection.

   Benchmark compile/20001226-1.c compilation time after changing this
   function.
   */

static inline bool
value_ranges_intersect_p (value_range_t *vr0, value_range_t *vr1)
{
  /* The value ranges do not intersect if the maximum of the first range is
     less than the minimum of the second range or vice versa.
     When those relations are unknown, we can't do any better.  */
  if (operand_less_p (vr0->max, vr1->min) != 0)
    return false;
  if (operand_less_p (vr1->max, vr0->min) != 0)
    return false;
  return true;
}


/* Return true if VR includes the value zero, false otherwise.  FIXME,
   currently this will return false for an anti-range like ~[-4, 3].
   This will be wrong when the semantics of value_inside_range are
   modified (currently the users of this function expect these
   semantics).  */

static inline bool
range_includes_zero_p (value_range_t *vr)
{
  tree zero;

  gcc_assert (vr->type != VR_UNDEFINED
              && vr->type != VR_VARYING
	      && !symbolic_range_p (vr));

  zero = build_int_cst (TREE_TYPE (vr->min), 0);
  return (value_inside_range (zero, vr) == 1);
}

/* Return true if T, an SSA_NAME, is known to be nonnegative.  Return
   false otherwise or if no value range information is available.  */

bool
ssa_name_nonnegative_p (const_tree t)
{
  value_range_t *vr = get_value_range (t);

  if (INTEGRAL_TYPE_P (t)
      && TYPE_UNSIGNED (t))
    return true;

  if (!vr)
    return false;

  /* Testing for VR_ANTI_RANGE is not useful here as any anti-range
     which would return a useful value should be encoded as a VR_RANGE.  */
  if (vr->type == VR_RANGE)
    {
      int result = compare_values (vr->min, integer_zero_node);

      return (result == 0 || result == 1);
    }
  return false;
}

/* If OP has a value range with a single constant value return that,
   otherwise return NULL_TREE.  This returns OP itself if OP is a
   constant.  */

static tree
op_with_constant_singleton_value_range (tree op)
{
  value_range_t *vr;

  if (is_gimple_min_invariant (op))
    return op;

  if (TREE_CODE (op) != SSA_NAME)
    return NULL_TREE;

  vr = get_value_range (op);
  if (vr->type == VR_RANGE
      && operand_equal_p (vr->min, vr->max, 0)
      && is_gimple_min_invariant (vr->min))
    return vr->min;

  return NULL_TREE;
}


/* Extract value range information from an ASSERT_EXPR EXPR and store
   it in *VR_P.  */

static void
extract_range_from_assert (value_range_t *vr_p, tree expr)
{
  tree var, cond, limit, min, max, type;
  value_range_t *var_vr, *limit_vr;
  enum tree_code cond_code;

  var = ASSERT_EXPR_VAR (expr);
  cond = ASSERT_EXPR_COND (expr);

  gcc_assert (COMPARISON_CLASS_P (cond));

  /* Find VAR in the ASSERT_EXPR conditional.  */
  if (var == TREE_OPERAND (cond, 0)
      || TREE_CODE (TREE_OPERAND (cond, 0)) == PLUS_EXPR
      || TREE_CODE (TREE_OPERAND (cond, 0)) == NOP_EXPR)
    {
      /* If the predicate is of the form VAR COMP LIMIT, then we just
	 take LIMIT from the RHS and use the same comparison code.  */
      cond_code = TREE_CODE (cond);
      limit = TREE_OPERAND (cond, 1);
      cond = TREE_OPERAND (cond, 0);
    }
  else
    {
      /* If the predicate is of the form LIMIT COMP VAR, then we need
	 to flip around the comparison code to create the proper range
	 for VAR.  */
      cond_code = swap_tree_comparison (TREE_CODE (cond));
      limit = TREE_OPERAND (cond, 0);
      cond = TREE_OPERAND (cond, 1);
    }

  limit = avoid_overflow_infinity (limit);

  type = TREE_TYPE (limit);
  gcc_assert (limit != var);

  /* For pointer arithmetic, we only keep track of pointer equality
     and inequality.  */
  if (POINTER_TYPE_P (type) && cond_code != NE_EXPR && cond_code != EQ_EXPR)
    {
      set_value_range_to_varying (vr_p);
      return;
    }

  /* If LIMIT is another SSA name and LIMIT has a range of its own,
     try to use LIMIT's range to avoid creating symbolic ranges
     unnecessarily. */
  limit_vr = (TREE_CODE (limit) == SSA_NAME) ? get_value_range (limit) : NULL;

  /* LIMIT's range is only interesting if it has any useful information.  */
  if (limit_vr
      && (limit_vr->type == VR_UNDEFINED
	  || limit_vr->type == VR_VARYING
	  || symbolic_range_p (limit_vr)))
    limit_vr = NULL;

  /* Initially, the new range has the same set of equivalences of
     VAR's range.  This will be revised before returning the final
     value.  Since assertions may be chained via mutually exclusive
     predicates, we will need to trim the set of equivalences before
     we are done.  */
  gcc_assert (vr_p->equiv == NULL);
  add_equivalence (&vr_p->equiv, var);

  /* Extract a new range based on the asserted comparison for VAR and
     LIMIT's value range.  Notice that if LIMIT has an anti-range, we
     will only use it for equality comparisons (EQ_EXPR).  For any
     other kind of assertion, we cannot derive a range from LIMIT's
     anti-range that can be used to describe the new range.  For
     instance, ASSERT_EXPR <x_2, x_2 <= b_4>.  If b_4 is ~[2, 10],
     then b_4 takes on the ranges [-INF, 1] and [11, +INF].  There is
     no single range for x_2 that could describe LE_EXPR, so we might
     as well build the range [b_4, +INF] for it.
     One special case we handle is extracting a range from a
     range test encoded as (unsigned)var + CST <= limit.  */
  if (TREE_CODE (cond) == NOP_EXPR
      || TREE_CODE (cond) == PLUS_EXPR)
    {
      if (TREE_CODE (cond) == PLUS_EXPR)
        {
          min = fold_build1 (NEGATE_EXPR, TREE_TYPE (TREE_OPERAND (cond, 1)),
			     TREE_OPERAND (cond, 1));
          max = int_const_binop (PLUS_EXPR, limit, min, 0);
	  cond = TREE_OPERAND (cond, 0);
	}
      else
	{
	  min = build_int_cst (TREE_TYPE (var), 0);
	  max = limit;
	}

      /* Make sure to not set TREE_OVERFLOW on the final type
	 conversion.  We are willingly interpreting large positive
	 unsigned values as negative singed values here.  */
      min = force_fit_type_double (TREE_TYPE (var), TREE_INT_CST_LOW (min),
				   TREE_INT_CST_HIGH (min), 0, false);
      max = force_fit_type_double (TREE_TYPE (var), TREE_INT_CST_LOW (max),
				   TREE_INT_CST_HIGH (max), 0, false);

      /* We can transform a max, min range to an anti-range or
         vice-versa.  Use set_and_canonicalize_value_range which does
	 this for us.  */
      if (cond_code == LE_EXPR)
        set_and_canonicalize_value_range (vr_p, VR_RANGE,
					  min, max, vr_p->equiv);
      else if (cond_code == GT_EXPR)
        set_and_canonicalize_value_range (vr_p, VR_ANTI_RANGE,
					  min, max, vr_p->equiv);
      else
	gcc_unreachable ();
    }
  else if (cond_code == EQ_EXPR)
    {
      enum value_range_type range_type;

      if (limit_vr)
	{
	  range_type = limit_vr->type;
	  min = limit_vr->min;
	  max = limit_vr->max;
	}
      else
	{
	  range_type = VR_RANGE;
	  min = limit;
	  max = limit;
	}

      set_value_range (vr_p, range_type, min, max, vr_p->equiv);

      /* When asserting the equality VAR == LIMIT and LIMIT is another
	 SSA name, the new range will also inherit the equivalence set
	 from LIMIT.  */
      if (TREE_CODE (limit) == SSA_NAME)
	add_equivalence (&vr_p->equiv, limit);
    }
  else if (cond_code == NE_EXPR)
    {
      /* As described above, when LIMIT's range is an anti-range and
	 this assertion is an inequality (NE_EXPR), then we cannot
	 derive anything from the anti-range.  For instance, if
	 LIMIT's range was ~[0, 0], the assertion 'VAR != LIMIT' does
	 not imply that VAR's range is [0, 0].  So, in the case of
	 anti-ranges, we just assert the inequality using LIMIT and
	 not its anti-range.

	 If LIMIT_VR is a range, we can only use it to build a new
	 anti-range if LIMIT_VR is a single-valued range.  For
	 instance, if LIMIT_VR is [0, 1], the predicate
	 VAR != [0, 1] does not mean that VAR's range is ~[0, 1].
	 Rather, it means that for value 0 VAR should be ~[0, 0]
	 and for value 1, VAR should be ~[1, 1].  We cannot
	 represent these ranges.

	 The only situation in which we can build a valid
	 anti-range is when LIMIT_VR is a single-valued range
	 (i.e., LIMIT_VR->MIN == LIMIT_VR->MAX).  In that case,
	 build the anti-range ~[LIMIT_VR->MIN, LIMIT_VR->MAX].  */
      if (limit_vr
	  && limit_vr->type == VR_RANGE
	  && compare_values (limit_vr->min, limit_vr->max) == 0)
	{
	  min = limit_vr->min;
	  max = limit_vr->max;
	}
      else
	{
	  /* In any other case, we cannot use LIMIT's range to build a
	     valid anti-range.  */
	  min = max = limit;
	}

      /* If MIN and MAX cover the whole range for their type, then
	 just use the original LIMIT.  */
      if (INTEGRAL_TYPE_P (type)
	  && vrp_val_is_min (min)
	  && vrp_val_is_max (max))
	min = max = limit;

      set_value_range (vr_p, VR_ANTI_RANGE, min, max, vr_p->equiv);
    }
  else if (cond_code == LE_EXPR || cond_code == LT_EXPR)
    {
      min = TYPE_MIN_VALUE (type);

      if (limit_vr == NULL || limit_vr->type == VR_ANTI_RANGE)
	max = limit;
      else
	{
	  /* If LIMIT_VR is of the form [N1, N2], we need to build the
	     range [MIN, N2] for LE_EXPR and [MIN, N2 - 1] for
	     LT_EXPR.  */
	  max = limit_vr->max;
	}

      /* If the maximum value forces us to be out of bounds, simply punt.
	 It would be pointless to try and do anything more since this
	 all should be optimized away above us.  */
      if ((cond_code == LT_EXPR
	   && compare_values (max, min) == 0)
	  || (CONSTANT_CLASS_P (max) && TREE_OVERFLOW (max)))
	set_value_range_to_varying (vr_p);
      else
	{
	  /* For LT_EXPR, we create the range [MIN, MAX - 1].  */
	  if (cond_code == LT_EXPR)
	    {
	      tree one = build_int_cst (type, 1);
	      max = fold_build2 (MINUS_EXPR, type, max, one);
	      if (EXPR_P (max))
		TREE_NO_WARNING (max) = 1;
	    }

	  set_value_range (vr_p, VR_RANGE, min, max, vr_p->equiv);
	}
    }
  else if (cond_code == GE_EXPR || cond_code == GT_EXPR)
    {
      max = TYPE_MAX_VALUE (type);

      if (limit_vr == NULL || limit_vr->type == VR_ANTI_RANGE)
	min = limit;
      else
	{
	  /* If LIMIT_VR is of the form [N1, N2], we need to build the
	     range [N1, MAX] for GE_EXPR and [N1 + 1, MAX] for
	     GT_EXPR.  */
	  min = limit_vr->min;
	}

      /* If the minimum value forces us to be out of bounds, simply punt.
	 It would be pointless to try and do anything more since this
	 all should be optimized away above us.  */
      if ((cond_code == GT_EXPR
	   && compare_values (min, max) == 0)
	  || (CONSTANT_CLASS_P (min) && TREE_OVERFLOW (min)))
	set_value_range_to_varying (vr_p);
      else
	{
	  /* For GT_EXPR, we create the range [MIN + 1, MAX].  */
	  if (cond_code == GT_EXPR)
	    {
	      tree one = build_int_cst (type, 1);
	      min = fold_build2 (PLUS_EXPR, type, min, one);
	      if (EXPR_P (min))
		TREE_NO_WARNING (min) = 1;
	    }

	  set_value_range (vr_p, VR_RANGE, min, max, vr_p->equiv);
	}
    }
  else
    gcc_unreachable ();

  /* If VAR already had a known range, it may happen that the new
     range we have computed and VAR's range are not compatible.  For
     instance,

	if (p_5 == NULL)
	  p_6 = ASSERT_EXPR <p_5, p_5 == NULL>;
	  x_7 = p_6->fld;
	  p_8 = ASSERT_EXPR <p_6, p_6 != NULL>;

     While the above comes from a faulty program, it will cause an ICE
     later because p_8 and p_6 will have incompatible ranges and at
     the same time will be considered equivalent.  A similar situation
     would arise from

     	if (i_5 > 10)
	  i_6 = ASSERT_EXPR <i_5, i_5 > 10>;
	  if (i_5 < 5)
	    i_7 = ASSERT_EXPR <i_6, i_6 < 5>;

     Again i_6 and i_7 will have incompatible ranges.  It would be
     pointless to try and do anything with i_7's range because
     anything dominated by 'if (i_5 < 5)' will be optimized away.
     Note, due to the wa in which simulation proceeds, the statement
     i_7 = ASSERT_EXPR <...> we would never be visited because the
     conditional 'if (i_5 < 5)' always evaluates to false.  However,
     this extra check does not hurt and may protect against future
     changes to VRP that may get into a situation similar to the
     NULL pointer dereference example.

     Note that these compatibility tests are only needed when dealing
     with ranges or a mix of range and anti-range.  If VAR_VR and VR_P
     are both anti-ranges, they will always be compatible, because two
     anti-ranges will always have a non-empty intersection.  */

  var_vr = get_value_range (var);

  /* We may need to make adjustments when VR_P and VAR_VR are numeric
     ranges or anti-ranges.  */
  if (vr_p->type == VR_VARYING
      || vr_p->type == VR_UNDEFINED
      || var_vr->type == VR_VARYING
      || var_vr->type == VR_UNDEFINED
      || symbolic_range_p (vr_p)
      || symbolic_range_p (var_vr))
    return;

  if (var_vr->type == VR_RANGE && vr_p->type == VR_RANGE)
    {
      /* If the two ranges have a non-empty intersection, we can
	 refine the resulting range.  Since the assert expression
	 creates an equivalency and at the same time it asserts a
	 predicate, we can take the intersection of the two ranges to
	 get better precision.  */
      if (value_ranges_intersect_p (var_vr, vr_p))
	{
	  /* Use the larger of the two minimums.  */
	  if (compare_values (vr_p->min, var_vr->min) == -1)
	    min = var_vr->min;
	  else
	    min = vr_p->min;

	  /* Use the smaller of the two maximums.  */
	  if (compare_values (vr_p->max, var_vr->max) == 1)
	    max = var_vr->max;
	  else
	    max = vr_p->max;

	  set_value_range (vr_p, vr_p->type, min, max, vr_p->equiv);
	}
      else
	{
	  /* The two ranges do not intersect, set the new range to
	     VARYING, because we will not be able to do anything
	     meaningful with it.  */
	  set_value_range_to_varying (vr_p);
	}
    }
  else if ((var_vr->type == VR_RANGE && vr_p->type == VR_ANTI_RANGE)
           || (var_vr->type == VR_ANTI_RANGE && vr_p->type == VR_RANGE))
    {
      /* A range and an anti-range will cancel each other only if
	 their ends are the same.  For instance, in the example above,
	 p_8's range ~[0, 0] and p_6's range [0, 0] are incompatible,
	 so VR_P should be set to VR_VARYING.  */
      if (compare_values (var_vr->min, vr_p->min) == 0
	  && compare_values (var_vr->max, vr_p->max) == 0)
	set_value_range_to_varying (vr_p);
      else
	{
	  tree min, max, anti_min, anti_max, real_min, real_max;
	  int cmp;

	  /* We want to compute the logical AND of the two ranges;
	     there are three cases to consider.


	     1. The VR_ANTI_RANGE range is completely within the
		VR_RANGE and the endpoints of the ranges are
		different.  In that case the resulting range
		should be whichever range is more precise.
		Typically that will be the VR_RANGE.

	     2. The VR_ANTI_RANGE is completely disjoint from
		the VR_RANGE.  In this case the resulting range
		should be the VR_RANGE.

	     3. There is some overlap between the VR_ANTI_RANGE
		and the VR_RANGE.

		3a. If the high limit of the VR_ANTI_RANGE resides
		    within the VR_RANGE, then the result is a new
		    VR_RANGE starting at the high limit of the
		    VR_ANTI_RANGE + 1 and extending to the
		    high limit of the original VR_RANGE.

		3b. If the low limit of the VR_ANTI_RANGE resides
		    within the VR_RANGE, then the result is a new
		    VR_RANGE starting at the low limit of the original
		    VR_RANGE and extending to the low limit of the
		    VR_ANTI_RANGE - 1.  */
	  if (vr_p->type == VR_ANTI_RANGE)
	    {
	      anti_min = vr_p->min;
	      anti_max = vr_p->max;
	      real_min = var_vr->min;
	      real_max = var_vr->max;
	    }
	  else
	    {
	      anti_min = var_vr->min;
	      anti_max = var_vr->max;
	      real_min = vr_p->min;
	      real_max = vr_p->max;
	    }


	  /* Case 1, VR_ANTI_RANGE completely within VR_RANGE,
	     not including any endpoints.  */
	  if (compare_values (anti_max, real_max) == -1
	      && compare_values (anti_min, real_min) == 1)
	    {
	      /* If the range is covering the whole valid range of
		 the type keep the anti-range.  */
	      if (!vrp_val_is_min (real_min)
		  || !vrp_val_is_max (real_max))
	        set_value_range (vr_p, VR_RANGE, real_min,
				 real_max, vr_p->equiv);
	    }
	  /* Case 2, VR_ANTI_RANGE completely disjoint from
	     VR_RANGE.  */
	  else if (compare_values (anti_min, real_max) == 1
		   || compare_values (anti_max, real_min) == -1)
	    {
	      set_value_range (vr_p, VR_RANGE, real_min,
			       real_max, vr_p->equiv);
	    }
	  /* Case 3a, the anti-range extends into the low
	     part of the real range.  Thus creating a new
	     low for the real range.  */
	  else if (((cmp = compare_values (anti_max, real_min)) == 1
		    || cmp == 0)
		   && compare_values (anti_max, real_max) == -1)
	    {
	      gcc_assert (!is_positive_overflow_infinity (anti_max));
	      if (needs_overflow_infinity (TREE_TYPE (anti_max))
		  && vrp_val_is_max (anti_max))
		{
		  if (!supports_overflow_infinity (TREE_TYPE (var_vr->min)))
		    {
		      set_value_range_to_varying (vr_p);
		      return;
		    }
		  min = positive_overflow_infinity (TREE_TYPE (var_vr->min));
		}
	      else if (!POINTER_TYPE_P (TREE_TYPE (var_vr->min)))
		min = fold_build2 (PLUS_EXPR, TREE_TYPE (var_vr->min),
				   anti_max,
				   build_int_cst (TREE_TYPE (var_vr->min), 1));
	      else
		min = fold_build2 (POINTER_PLUS_EXPR, TREE_TYPE (var_vr->min),
				   anti_max, size_int (1));
	      max = real_max;
	      set_value_range (vr_p, VR_RANGE, min, max, vr_p->equiv);
	    }
	  /* Case 3b, the anti-range extends into the high
	     part of the real range.  Thus creating a new
	     higher for the real range.  */
	  else if (compare_values (anti_min, real_min) == 1
		   && ((cmp = compare_values (anti_min, real_max)) == -1
		       || cmp == 0))
	    {
	      gcc_assert (!is_negative_overflow_infinity (anti_min));
	      if (needs_overflow_infinity (TREE_TYPE (anti_min))
		  && vrp_val_is_min (anti_min))
		{
		  if (!supports_overflow_infinity (TREE_TYPE (var_vr->min)))
		    {
		      set_value_range_to_varying (vr_p);
		      return;
		    }
		  max = negative_overflow_infinity (TREE_TYPE (var_vr->min));
		}
	      else if (!POINTER_TYPE_P (TREE_TYPE (var_vr->min)))
		max = fold_build2 (MINUS_EXPR, TREE_TYPE (var_vr->min),
				   anti_min,
				   build_int_cst (TREE_TYPE (var_vr->min), 1));
	      else
		max = fold_build2 (POINTER_PLUS_EXPR, TREE_TYPE (var_vr->min),
				   anti_min,
				   size_int (-1));
	      min = real_min;
	      set_value_range (vr_p, VR_RANGE, min, max, vr_p->equiv);
	    }
	}
    }
}


/* Extract range information from SSA name VAR and store it in VR.  If
   VAR has an interesting range, use it.  Otherwise, create the
   range [VAR, VAR] and return it.  This is useful in situations where
   we may have conditionals testing values of VARYING names.  For
   instance,

   	x_3 = y_5;
	if (x_3 > y_5)
	  ...

    Even if y_5 is deemed VARYING, we can determine that x_3 > y_5 is
    always false.  */

static void
extract_range_from_ssa_name (value_range_t *vr, tree var)
{
  value_range_t *var_vr = get_value_range (var);

  if (var_vr->type != VR_UNDEFINED && var_vr->type != VR_VARYING)
    copy_value_range (vr, var_vr);
  else
    set_value_range (vr, VR_RANGE, var, var, NULL);

  add_equivalence (&vr->equiv, var);
}


/* Wrapper around int_const_binop.  If the operation overflows and we
   are not using wrapping arithmetic, then adjust the result to be
   -INF or +INF depending on CODE, VAL1 and VAL2.  This can return
   NULL_TREE if we need to use an overflow infinity representation but
   the type does not support it.  */

static tree
vrp_int_const_binop (enum tree_code code, tree val1, tree val2)
{
  tree res;

  res = int_const_binop (code, val1, val2, 0);

  /* If we are using unsigned arithmetic, operate symbolically
     on -INF and +INF as int_const_binop only handles signed overflow.  */
  if (TYPE_UNSIGNED (TREE_TYPE (val1)))
    {
      int checkz = compare_values (res, val1);
      bool overflow = false;

      /* Ensure that res = val1 [+*] val2 >= val1
         or that res = val1 - val2 <= val1.  */
      if ((code == PLUS_EXPR
	   && !(checkz == 1 || checkz == 0))
          || (code == MINUS_EXPR
	      && !(checkz == 0 || checkz == -1)))
	{
	  overflow = true;
	}
      /* Checking for multiplication overflow is done by dividing the
	 output of the multiplication by the first input of the
	 multiplication.  If the result of that division operation is
	 not equal to the second input of the multiplication, then the
	 multiplication overflowed.  */
      else if (code == MULT_EXPR && !integer_zerop (val1))
	{
	  tree tmp = int_const_binop (TRUNC_DIV_EXPR,
				      res,
				      val1, 0);
	  int check = compare_values (tmp, val2);

	  if (check != 0)
	    overflow = true;
	}

      if (overflow)
	{
	  res = copy_node (res);
	  TREE_OVERFLOW (res) = 1;
	}

    }
  else if (TYPE_OVERFLOW_WRAPS (TREE_TYPE (val1)))
    /* If the singed operation wraps then int_const_binop has done
       everything we want.  */
    ;
  else if ((TREE_OVERFLOW (res)
	    && !TREE_OVERFLOW (val1)
	    && !TREE_OVERFLOW (val2))
	   || is_overflow_infinity (val1)
	   || is_overflow_infinity (val2))
    {
      /* If the operation overflowed but neither VAL1 nor VAL2 are
	 overflown, return -INF or +INF depending on the operation
	 and the combination of signs of the operands.  */
      int sgn1 = tree_int_cst_sgn (val1);
      int sgn2 = tree_int_cst_sgn (val2);

      if (needs_overflow_infinity (TREE_TYPE (res))
	  && !supports_overflow_infinity (TREE_TYPE (res)))
	return NULL_TREE;

      /* We have to punt on adding infinities of different signs,
	 since we can't tell what the sign of the result should be.
	 Likewise for subtracting infinities of the same sign.  */
      if (((code == PLUS_EXPR && sgn1 != sgn2)
	   || (code == MINUS_EXPR && sgn1 == sgn2))
	  && is_overflow_infinity (val1)
	  && is_overflow_infinity (val2))
	return NULL_TREE;

      /* Don't try to handle division or shifting of infinities.  */
      if ((code == TRUNC_DIV_EXPR
	   || code == FLOOR_DIV_EXPR
	   || code == CEIL_DIV_EXPR
	   || code == EXACT_DIV_EXPR
	   || code == ROUND_DIV_EXPR
	   || code == RSHIFT_EXPR)
	  && (is_overflow_infinity (val1)
	      || is_overflow_infinity (val2)))
	return NULL_TREE;

      /* Notice that we only need to handle the restricted set of
	 operations handled by extract_range_from_binary_expr.
	 Among them, only multiplication, addition and subtraction
	 can yield overflow without overflown operands because we
	 are working with integral types only... except in the
	 case VAL1 = -INF and VAL2 = -1 which overflows to +INF
	 for division too.  */

      /* For multiplication, the sign of the overflow is given
	 by the comparison of the signs of the operands.  */
      if ((code == MULT_EXPR && sgn1 == sgn2)
          /* For addition, the operands must be of the same sign
	     to yield an overflow.  Its sign is therefore that
	     of one of the operands, for example the first.  For
	     infinite operands X + -INF is negative, not positive.  */
	  || (code == PLUS_EXPR
	      && (sgn1 >= 0
		  ? !is_negative_overflow_infinity (val2)
		  : is_positive_overflow_infinity (val2)))
	  /* For subtraction, non-infinite operands must be of
	     different signs to yield an overflow.  Its sign is
	     therefore that of the first operand or the opposite of
	     that of the second operand.  A first operand of 0 counts
	     as positive here, for the corner case 0 - (-INF), which
	     overflows, but must yield +INF.  For infinite operands 0
	     - INF is negative, not positive.  */
	  || (code == MINUS_EXPR
	      && (sgn1 >= 0
		  ? !is_positive_overflow_infinity (val2)
		  : is_negative_overflow_infinity (val2)))
	  /* We only get in here with positive shift count, so the
	     overflow direction is the same as the sign of val1.
	     Actually rshift does not overflow at all, but we only
	     handle the case of shifting overflowed -INF and +INF.  */
	  || (code == RSHIFT_EXPR
	      && sgn1 >= 0)
	  /* For division, the only case is -INF / -1 = +INF.  */
	  || code == TRUNC_DIV_EXPR
	  || code == FLOOR_DIV_EXPR
	  || code == CEIL_DIV_EXPR
	  || code == EXACT_DIV_EXPR
	  || code == ROUND_DIV_EXPR)
	return (needs_overflow_infinity (TREE_TYPE (res))
		? positive_overflow_infinity (TREE_TYPE (res))
		: TYPE_MAX_VALUE (TREE_TYPE (res)));
      else
	return (needs_overflow_infinity (TREE_TYPE (res))
		? negative_overflow_infinity (TREE_TYPE (res))
		: TYPE_MIN_VALUE (TREE_TYPE (res)));
    }

  return res;
}


/* Extract range information from a binary expression EXPR based on
   the ranges of each of its operands and the expression code.  */

static void
extract_range_from_binary_expr (value_range_t *vr,
				enum tree_code code,
				tree expr_type, tree op0, tree op1)
{
  enum value_range_type type;
  tree min, max;
  int cmp;
  value_range_t vr0 = { VR_UNDEFINED, NULL_TREE, NULL_TREE, NULL };
  value_range_t vr1 = { VR_UNDEFINED, NULL_TREE, NULL_TREE, NULL };

  /* Not all binary expressions can be applied to ranges in a
     meaningful way.  Handle only arithmetic operations.  */
  if (code != PLUS_EXPR
      && code != MINUS_EXPR
      && code != POINTER_PLUS_EXPR
      && code != MULT_EXPR
      && code != TRUNC_DIV_EXPR
      && code != FLOOR_DIV_EXPR
      && code != CEIL_DIV_EXPR
      && code != EXACT_DIV_EXPR
      && code != ROUND_DIV_EXPR
      && code != TRUNC_MOD_EXPR
      && code != RSHIFT_EXPR
      && code != MIN_EXPR
      && code != MAX_EXPR
      && code != BIT_AND_EXPR
      && code != BIT_IOR_EXPR
      && code != TRUTH_AND_EXPR
      && code != TRUTH_OR_EXPR)
    {
      /* We can still do constant propagation here.  */
      tree const_op0 = op_with_constant_singleton_value_range (op0);
      tree const_op1 = op_with_constant_singleton_value_range (op1);
      if (const_op0 || const_op1)
	{
	  tree tem = fold_binary (code, expr_type,
				  const_op0 ? const_op0 : op0,
				  const_op1 ? const_op1 : op1);
	  if (tem
	      && is_gimple_min_invariant (tem)
	      && !is_overflow_infinity (tem))
	    {
	      set_value_range (vr, VR_RANGE, tem, tem, NULL);
	      return;
	    }
	}
      set_value_range_to_varying (vr);
      return;
    }

  /* Get value ranges for each operand.  For constant operands, create
     a new value range with the operand to simplify processing.  */
  if (TREE_CODE (op0) == SSA_NAME)
    vr0 = *(get_value_range (op0));
  else if (is_gimple_min_invariant (op0))
    set_value_range_to_value (&vr0, op0, NULL);
  else
    set_value_range_to_varying (&vr0);

  if (TREE_CODE (op1) == SSA_NAME)
    vr1 = *(get_value_range (op1));
  else if (is_gimple_min_invariant (op1))
    set_value_range_to_value (&vr1, op1, NULL);
  else
    set_value_range_to_varying (&vr1);

  /* If either range is UNDEFINED, so is the result.  */
  if (vr0.type == VR_UNDEFINED || vr1.type == VR_UNDEFINED)
    {
      set_value_range_to_undefined (vr);
      return;
    }

  /* The type of the resulting value range defaults to VR0.TYPE.  */
  type = vr0.type;

  /* Refuse to operate on VARYING ranges, ranges of different kinds
     and symbolic ranges.  As an exception, we allow BIT_AND_EXPR
     because we may be able to derive a useful range even if one of
     the operands is VR_VARYING or symbolic range.  Similarly for
     divisions.  TODO, we may be able to derive anti-ranges in
     some cases.  */
  if (code != BIT_AND_EXPR
      && code != TRUTH_AND_EXPR
      && code != TRUTH_OR_EXPR
      && code != TRUNC_DIV_EXPR
      && code != FLOOR_DIV_EXPR
      && code != CEIL_DIV_EXPR
      && code != EXACT_DIV_EXPR
      && code != ROUND_DIV_EXPR
      && code != TRUNC_MOD_EXPR
      && (vr0.type == VR_VARYING
	  || vr1.type == VR_VARYING
	  || vr0.type != vr1.type
	  || symbolic_range_p (&vr0)
	  || symbolic_range_p (&vr1)))
    {
      set_value_range_to_varying (vr);
      return;
    }

  /* Now evaluate the expression to determine the new range.  */
  if (POINTER_TYPE_P (expr_type)
      || POINTER_TYPE_P (TREE_TYPE (op0))
      || POINTER_TYPE_P (TREE_TYPE (op1)))
    {
      if (code == MIN_EXPR || code == MAX_EXPR)
	{
	  /* For MIN/MAX expressions with pointers, we only care about
	     nullness, if both are non null, then the result is nonnull.
	     If both are null, then the result is null. Otherwise they
	     are varying.  */
	  if (range_is_nonnull (&vr0) && range_is_nonnull (&vr1))
	    set_value_range_to_nonnull (vr, expr_type);
	  else if (range_is_null (&vr0) && range_is_null (&vr1))
	    set_value_range_to_null (vr, expr_type);
	  else
	    set_value_range_to_varying (vr);

	  return;
	}
      gcc_assert (code == POINTER_PLUS_EXPR);
      /* For pointer types, we are really only interested in asserting
	 whether the expression evaluates to non-NULL.  */
      if (range_is_nonnull (&vr0) || range_is_nonnull (&vr1))
	set_value_range_to_nonnull (vr, expr_type);
      else if (range_is_null (&vr0) && range_is_null (&vr1))
	set_value_range_to_null (vr, expr_type);
      else
	set_value_range_to_varying (vr);

      return;
    }

  /* For integer ranges, apply the operation to each end of the
     range and see what we end up with.  */
  if (code == TRUTH_AND_EXPR
      || code == TRUTH_OR_EXPR)
    {
      /* If one of the operands is zero, we know that the whole
	 expression evaluates zero.  */
      if (code == TRUTH_AND_EXPR
	  && ((vr0.type == VR_RANGE
	       && integer_zerop (vr0.min)
	       && integer_zerop (vr0.max))
	      || (vr1.type == VR_RANGE
		  && integer_zerop (vr1.min)
		  && integer_zerop (vr1.max))))
	{
	  type = VR_RANGE;
	  min = max = build_int_cst (expr_type, 0);
	}
      /* If one of the operands is one, we know that the whole
	 expression evaluates one.  */
      else if (code == TRUTH_OR_EXPR
	       && ((vr0.type == VR_RANGE
		    && integer_onep (vr0.min)
		    && integer_onep (vr0.max))
		   || (vr1.type == VR_RANGE
		       && integer_onep (vr1.min)
		       && integer_onep (vr1.max))))
	{
	  type = VR_RANGE;
	  min = max = build_int_cst (expr_type, 1);
	}
      else if (vr0.type != VR_VARYING
	       && vr1.type != VR_VARYING
	       && vr0.type == vr1.type
	       && !symbolic_range_p (&vr0)
	       && !overflow_infinity_range_p (&vr0)
	       && !symbolic_range_p (&vr1)
	       && !overflow_infinity_range_p (&vr1))
	{
	  /* Boolean expressions cannot be folded with int_const_binop.  */
	  min = fold_binary (code, expr_type, vr0.min, vr1.min);
	  max = fold_binary (code, expr_type, vr0.max, vr1.max);
	}
      else
	{
	  /* The result of a TRUTH_*_EXPR is always true or false.  */
	  set_value_range_to_truthvalue (vr, expr_type);
	  return;
	}
    }
  else if (code == PLUS_EXPR
	   || code == MIN_EXPR
	   || code == MAX_EXPR)
    {
      /* If we have a PLUS_EXPR with two VR_ANTI_RANGEs, drop to
	 VR_VARYING.  It would take more effort to compute a precise
	 range for such a case.  For example, if we have op0 == 1 and
	 op1 == -1 with their ranges both being ~[0,0], we would have
	 op0 + op1 == 0, so we cannot claim that the sum is in ~[0,0].
	 Note that we are guaranteed to have vr0.type == vr1.type at
	 this point.  */
      if (code == PLUS_EXPR && vr0.type == VR_ANTI_RANGE)
	{
	  set_value_range_to_varying (vr);
	  return;
	}

      /* For operations that make the resulting range directly
	 proportional to the original ranges, apply the operation to
	 the same end of each range.  */
      min = vrp_int_const_binop (code, vr0.min, vr1.min);
      max = vrp_int_const_binop (code, vr0.max, vr1.max);

      /* If both additions overflowed the range kind is still correct.
	 This happens regularly with subtracting something in unsigned
	 arithmetic.
         ???  See PR30318 for all the cases we do not handle.  */
      if (code == PLUS_EXPR
	  && (TREE_OVERFLOW (min) && !is_overflow_infinity (min))
	  && (TREE_OVERFLOW (max) && !is_overflow_infinity (max)))
	{
	  min = build_int_cst_wide (TREE_TYPE (min),
				    TREE_INT_CST_LOW (min),
				    TREE_INT_CST_HIGH (min));
	  max = build_int_cst_wide (TREE_TYPE (max),
				    TREE_INT_CST_LOW (max),
				    TREE_INT_CST_HIGH (max));
	}
    }
  else if (code == MULT_EXPR
	   || code == TRUNC_DIV_EXPR
	   || code == FLOOR_DIV_EXPR
	   || code == CEIL_DIV_EXPR
	   || code == EXACT_DIV_EXPR
	   || code == ROUND_DIV_EXPR
	   || code == RSHIFT_EXPR)
    {
      tree val[4];
      size_t i;
      bool sop;

      /* If we have an unsigned MULT_EXPR with two VR_ANTI_RANGEs,
	 drop to VR_VARYING.  It would take more effort to compute a
	 precise range for such a case.  For example, if we have
	 op0 == 65536 and op1 == 65536 with their ranges both being
	 ~[0,0] on a 32-bit machine, we would have op0 * op1 == 0, so
	 we cannot claim that the product is in ~[0,0].  Note that we
	 are guaranteed to have vr0.type == vr1.type at this
	 point.  */
      if (code == MULT_EXPR
	  && vr0.type == VR_ANTI_RANGE
	  && !TYPE_OVERFLOW_UNDEFINED (TREE_TYPE (op0)))
	{
	  set_value_range_to_varying (vr);
	  return;
	}

      /* If we have a RSHIFT_EXPR with any shift values outside [0..prec-1],
	 then drop to VR_VARYING.  Outside of this range we get undefined
	 behavior from the shift operation.  We cannot even trust
	 SHIFT_COUNT_TRUNCATED at this stage, because that applies to rtl
	 shifts, and the operation at the tree level may be widened.  */
      if (code == RSHIFT_EXPR)
	{
	  if (vr1.type == VR_ANTI_RANGE
	      || !vrp_expr_computes_nonnegative (op1, &sop)
	      || (operand_less_p
		  (build_int_cst (TREE_TYPE (vr1.max),
				  TYPE_PRECISION (expr_type) - 1),
		   vr1.max) != 0))
	    {
	      set_value_range_to_varying (vr);
	      return;
	    }
	}

      else if ((code == TRUNC_DIV_EXPR
		|| code == FLOOR_DIV_EXPR
		|| code == CEIL_DIV_EXPR
		|| code == EXACT_DIV_EXPR
		|| code == ROUND_DIV_EXPR)
	       && (vr0.type != VR_RANGE || symbolic_range_p (&vr0)))
	{
	  /* For division, if op1 has VR_RANGE but op0 does not, something
	     can be deduced just from that range.  Say [min, max] / [4, max]
	     gives [min / 4, max / 4] range.  */
	  if (vr1.type == VR_RANGE
	      && !symbolic_range_p (&vr1)
	      && !range_includes_zero_p (&vr1))
	    {
	      vr0.type = type = VR_RANGE;
	      vr0.min = vrp_val_min (TREE_TYPE (op0));
	      vr0.max = vrp_val_max (TREE_TYPE (op1));
	    }
	  else
	    {
	      set_value_range_to_varying (vr);
	      return;
	    }
	}

      /* For divisions, if op0 is VR_RANGE, we can deduce a range
	 even if op1 is VR_VARYING, VR_ANTI_RANGE, symbolic or can
	 include 0.  */
      if ((code == TRUNC_DIV_EXPR
	   || code == FLOOR_DIV_EXPR
	   || code == CEIL_DIV_EXPR
	   || code == EXACT_DIV_EXPR
	   || code == ROUND_DIV_EXPR)
	  && vr0.type == VR_RANGE
	  && (vr1.type != VR_RANGE
	      || symbolic_range_p (&vr1)
	      || range_includes_zero_p (&vr1)))
	{
	  tree zero = build_int_cst (TREE_TYPE (vr0.min), 0);
	  int cmp;

	  sop = false;
	  min = NULL_TREE;
	  max = NULL_TREE;
	  if (vrp_expr_computes_nonnegative (op1, &sop) && !sop)
	    {
	      /* For unsigned division or when divisor is known
		 to be non-negative, the range has to cover
		 all numbers from 0 to max for positive max
		 and all numbers from min to 0 for negative min.  */
	      cmp = compare_values (vr0.max, zero);
	      if (cmp == -1)
		max = zero;
	      else if (cmp == 0 || cmp == 1)
		max = vr0.max;
	      else
		type = VR_VARYING;
	      cmp = compare_values (vr0.min, zero);
	      if (cmp == 1)
		min = zero;
	      else if (cmp == 0 || cmp == -1)
		min = vr0.min;
	      else
		type = VR_VARYING;
	    }
	  else
	    {
	      /* Otherwise the range is -max .. max or min .. -min
		 depending on which bound is bigger in absolute value,
		 as the division can change the sign.  */
	      abs_extent_range (vr, vr0.min, vr0.max);
	      return;
	    }
	  if (type == VR_VARYING)
	    {
	      set_value_range_to_varying (vr);
	      return;
	    }
	}

      /* Multiplications and divisions are a bit tricky to handle,
	 depending on the mix of signs we have in the two ranges, we
	 need to operate on different values to get the minimum and
	 maximum values for the new range.  One approach is to figure
	 out all the variations of range combinations and do the
	 operations.

	 However, this involves several calls to compare_values and it
	 is pretty convoluted.  It's simpler to do the 4 operations
	 (MIN0 OP MIN1, MIN0 OP MAX1, MAX0 OP MIN1 and MAX0 OP MAX0 OP
	 MAX1) and then figure the smallest and largest values to form
	 the new range.  */
      else
	{
	  gcc_assert ((vr0.type == VR_RANGE
		       || (code == MULT_EXPR && vr0.type == VR_ANTI_RANGE))
		      && vr0.type == vr1.type);

	  /* Compute the 4 cross operations.  */
	  sop = false;
	  val[0] = vrp_int_const_binop (code, vr0.min, vr1.min);
	  if (val[0] == NULL_TREE)
	    sop = true;

	  if (vr1.max == vr1.min)
	    val[1] = NULL_TREE;
	  else
	    {
	      val[1] = vrp_int_const_binop (code, vr0.min, vr1.max);
	      if (val[1] == NULL_TREE)
		sop = true;
	    }

	  if (vr0.max == vr0.min)
	    val[2] = NULL_TREE;
	  else
	    {
	      val[2] = vrp_int_const_binop (code, vr0.max, vr1.min);
	      if (val[2] == NULL_TREE)
		sop = true;
	    }

	  if (vr0.min == vr0.max || vr1.min == vr1.max)
	    val[3] = NULL_TREE;
	  else
	    {
	      val[3] = vrp_int_const_binop (code, vr0.max, vr1.max);
	      if (val[3] == NULL_TREE)
		sop = true;
	    }

	  if (sop)
	    {
	      set_value_range_to_varying (vr);
	      return;
	    }

	  /* Set MIN to the minimum of VAL[i] and MAX to the maximum
	     of VAL[i].  */
	  min = val[0];
	  max = val[0];
	  for (i = 1; i < 4; i++)
	    {
	      if (!is_gimple_min_invariant (min)
		  || (TREE_OVERFLOW (min) && !is_overflow_infinity (min))
		  || !is_gimple_min_invariant (max)
		  || (TREE_OVERFLOW (max) && !is_overflow_infinity (max)))
		break;

	      if (val[i])
		{
		  if (!is_gimple_min_invariant (val[i])
		      || (TREE_OVERFLOW (val[i])
			  && !is_overflow_infinity (val[i])))
		    {
		      /* If we found an overflowed value, set MIN and MAX
			 to it so that we set the resulting range to
			 VARYING.  */
		      min = max = val[i];
		      break;
		    }

		  if (compare_values (val[i], min) == -1)
		    min = val[i];

		  if (compare_values (val[i], max) == 1)
		    max = val[i];
		}
	    }
	}
    }
  else if (code == TRUNC_MOD_EXPR)
    {
      bool sop = false;
      if (vr1.type != VR_RANGE
	  || symbolic_range_p (&vr1)
	  || range_includes_zero_p (&vr1)
	  || vrp_val_is_min (vr1.min))
	{
	  set_value_range_to_varying (vr);
	  return;
	}
      type = VR_RANGE;
      /* Compute MAX <|vr1.min|, |vr1.max|> - 1.  */
      max = fold_unary_to_constant (ABS_EXPR, TREE_TYPE (vr1.min), vr1.min);
      if (tree_int_cst_lt (max, vr1.max))
	max = vr1.max;
      max = int_const_binop (MINUS_EXPR, max, integer_one_node, 0);
      /* If the dividend is non-negative the modulus will be
	 non-negative as well.  */
      if (TYPE_UNSIGNED (TREE_TYPE (max))
	  || (vrp_expr_computes_nonnegative (op0, &sop) && !sop))
	min = build_int_cst (TREE_TYPE (max), 0);
      else
	min = fold_unary_to_constant (NEGATE_EXPR, TREE_TYPE (max), max);
    }
  else if (code == MINUS_EXPR)
    {
      /* If we have a MINUS_EXPR with two VR_ANTI_RANGEs, drop to
	 VR_VARYING.  It would take more effort to compute a precise
	 range for such a case.  For example, if we have op0 == 1 and
	 op1 == 1 with their ranges both being ~[0,0], we would have
	 op0 - op1 == 0, so we cannot claim that the difference is in
	 ~[0,0].  Note that we are guaranteed to have
	 vr0.type == vr1.type at this point.  */
      if (vr0.type == VR_ANTI_RANGE)
	{
	  set_value_range_to_varying (vr);
	  return;
	}

      /* For MINUS_EXPR, apply the operation to the opposite ends of
	 each range.  */
      min = vrp_int_const_binop (code, vr0.min, vr1.max);
      max = vrp_int_const_binop (code, vr0.max, vr1.min);
    }
  else if (code == BIT_AND_EXPR)
    {
      bool vr0_int_cst_singleton_p, vr1_int_cst_singleton_p;

      vr0_int_cst_singleton_p = range_int_cst_singleton_p (&vr0);
      vr1_int_cst_singleton_p = range_int_cst_singleton_p (&vr1);

      if (vr0_int_cst_singleton_p && vr1_int_cst_singleton_p)
	min = max = int_const_binop (code, vr0.max, vr1.max, 0);
      else if (vr0_int_cst_singleton_p
	       && tree_int_cst_sgn (vr0.max) >= 0)
	{
	  min = build_int_cst (expr_type, 0);
	  max = vr0.max;
	}
      else if (vr1_int_cst_singleton_p
	       && tree_int_cst_sgn (vr1.max) >= 0)
	{
	  type = VR_RANGE;
	  min = build_int_cst (expr_type, 0);
	  max = vr1.max;
	}
      else
	{
	  set_value_range_to_varying (vr);
	  return;
	}
    }
  else if (code == BIT_IOR_EXPR)
    {
      if (range_int_cst_p (&vr0)
	  && range_int_cst_p (&vr1)
	  && tree_int_cst_sgn (vr0.min) >= 0
	  && tree_int_cst_sgn (vr1.min) >= 0)
	{
	  double_int vr0_max = tree_to_double_int (vr0.max);
	  double_int vr1_max = tree_to_double_int (vr1.max);
	  double_int ior_max;

	  /* Set all bits to the right of the most significant one to 1.
	     For example, [0, 4] | [4, 4] = [4, 7]. */
	  ior_max.low = vr0_max.low | vr1_max.low;
	  ior_max.high = vr0_max.high | vr1_max.high;
	  if (ior_max.high != 0)
	    {
	      ior_max.low = ~(unsigned HOST_WIDE_INT)0u;
	      ior_max.high |= ((HOST_WIDE_INT) 1
			       << floor_log2 (ior_max.high)) - 1;
	    }
	  else if (ior_max.low != 0)
	    ior_max.low |= ((unsigned HOST_WIDE_INT) 1u
			    << floor_log2 (ior_max.low)) - 1;

	  /* Both of these endpoints are conservative.  */
          min = vrp_int_const_binop (MAX_EXPR, vr0.min, vr1.min);
          max = double_int_to_tree (expr_type, ior_max);
	}
      else
	{
	  set_value_range_to_varying (vr);
	  return;
	}
    }
  else
    gcc_unreachable ();

  /* If either MIN or MAX overflowed, then set the resulting range to
     VARYING.  But we do accept an overflow infinity
     representation.  */
  if (min == NULL_TREE
      || !is_gimple_min_invariant (min)
      || (TREE_OVERFLOW (min) && !is_overflow_infinity (min))
      || max == NULL_TREE
      || !is_gimple_min_invariant (max)
      || (TREE_OVERFLOW (max) && !is_overflow_infinity (max)))
    {
      set_value_range_to_varying (vr);
      return;
    }

  /* We punt if:
     1) [-INF, +INF]
     2) [-INF, +-INF(OVF)]
     3) [+-INF(OVF), +INF]
     4) [+-INF(OVF), +-INF(OVF)]
     We learn nothing when we have INF and INF(OVF) on both sides.
     Note that we do accept [-INF, -INF] and [+INF, +INF] without
     overflow.  */
  if ((vrp_val_is_min (min) || is_overflow_infinity (min))
      && (vrp_val_is_max (max) || is_overflow_infinity (max)))
    {
      set_value_range_to_varying (vr);
      return;
    }

  cmp = compare_values (min, max);
  if (cmp == -2 || cmp == 1)
    {
      /* If the new range has its limits swapped around (MIN > MAX),
	 then the operation caused one of them to wrap around, mark
	 the new range VARYING.  */
      set_value_range_to_varying (vr);
    }
  else
    set_value_range (vr, type, min, max, NULL);
}


/* Extract range information from a unary expression EXPR based on
   the range of its operand and the expression code.  */

static void
extract_range_from_unary_expr (value_range_t *vr, enum tree_code code,
			       tree type, tree op0)
{
  tree min, max;
  int cmp;
  value_range_t vr0 = { VR_UNDEFINED, NULL_TREE, NULL_TREE, NULL };

  /* Refuse to operate on certain unary expressions for which we
     cannot easily determine a resulting range.  */
  if (code == FIX_TRUNC_EXPR
      || code == FLOAT_EXPR
      || code == BIT_NOT_EXPR
      || code == CONJ_EXPR)
    {
      /* We can still do constant propagation here.  */
      if ((op0 = op_with_constant_singleton_value_range (op0)) != NULL_TREE)
	{
	  tree tem = fold_unary (code, type, op0);
	  if (tem
	      && is_gimple_min_invariant (tem)
	      && !is_overflow_infinity (tem))
	    {
	      set_value_range (vr, VR_RANGE, tem, tem, NULL);
	      return;
	    }
	}
      set_value_range_to_varying (vr);
      return;
    }

  /* Get value ranges for the operand.  For constant operands, create
     a new value range with the operand to simplify processing.  */
  if (TREE_CODE (op0) == SSA_NAME)
    vr0 = *(get_value_range (op0));
  else if (is_gimple_min_invariant (op0))
    set_value_range_to_value (&vr0, op0, NULL);
  else
    set_value_range_to_varying (&vr0);

  /* If VR0 is UNDEFINED, so is the result.  */
  if (vr0.type == VR_UNDEFINED)
    {
      set_value_range_to_undefined (vr);
      return;
    }

  /* Refuse to operate on symbolic ranges, or if neither operand is
     a pointer or integral type.  */
  if ((!INTEGRAL_TYPE_P (TREE_TYPE (op0))
       && !POINTER_TYPE_P (TREE_TYPE (op0)))
      || (vr0.type != VR_VARYING
	  && symbolic_range_p (&vr0)))
    {
      set_value_range_to_varying (vr);
      return;
    }

  /* If the expression involves pointers, we are only interested in
     determining if it evaluates to NULL [0, 0] or non-NULL (~[0, 0]).  */
  if (POINTER_TYPE_P (type) || POINTER_TYPE_P (TREE_TYPE (op0)))
    {
      bool sop;

      sop = false;
      if (range_is_nonnull (&vr0)
	  || (tree_unary_nonzero_warnv_p (code, type, op0, &sop)
	      && !sop))
	set_value_range_to_nonnull (vr, type);
      else if (range_is_null (&vr0))
	set_value_range_to_null (vr, type);
      else
	set_value_range_to_varying (vr);

      return;
    }

  /* Handle unary expressions on integer ranges.  */
  if (CONVERT_EXPR_CODE_P (code)
      && INTEGRAL_TYPE_P (type)
      && INTEGRAL_TYPE_P (TREE_TYPE (op0)))
    {
      tree inner_type = TREE_TYPE (op0);
      tree outer_type = type;

      /* If VR0 is varying and we increase the type precision, assume
	 a full range for the following transformation.  */
      if (vr0.type == VR_VARYING
	  && TYPE_PRECISION (inner_type) < TYPE_PRECISION (outer_type))
	{
	  vr0.type = VR_RANGE;
	  vr0.min = TYPE_MIN_VALUE (inner_type);
	  vr0.max = TYPE_MAX_VALUE (inner_type);
	}

      /* If VR0 is a constant range or anti-range and the conversion is
	 not truncating we can convert the min and max values and
	 canonicalize the resulting range.  Otherwise we can do the
	 conversion if the size of the range is less than what the
	 precision of the target type can represent and the range is
	 not an anti-range.  */
      if ((vr0.type == VR_RANGE
	   || vr0.type == VR_ANTI_RANGE)
	  && TREE_CODE (vr0.min) == INTEGER_CST
	  && TREE_CODE (vr0.max) == INTEGER_CST
	  && (!is_overflow_infinity (vr0.min)
	      || (vr0.type == VR_RANGE
		  && TYPE_PRECISION (outer_type) > TYPE_PRECISION (inner_type)
		  && needs_overflow_infinity (outer_type)
		  && supports_overflow_infinity (outer_type)))
	  && (!is_overflow_infinity (vr0.max)
	      || (vr0.type == VR_RANGE
		  && TYPE_PRECISION (outer_type) > TYPE_PRECISION (inner_type)
		  && needs_overflow_infinity (outer_type)
		  && supports_overflow_infinity (outer_type)))
	  && (TYPE_PRECISION (outer_type) >= TYPE_PRECISION (inner_type)
	      || (vr0.type == VR_RANGE
		  && integer_zerop (int_const_binop (RSHIFT_EXPR,
		       int_const_binop (MINUS_EXPR, vr0.max, vr0.min, 0),
		         size_int (TYPE_PRECISION (outer_type)), 0)))))
	{
	  tree new_min, new_max;
	  new_min = force_fit_type_double (outer_type,
					   TREE_INT_CST_LOW (vr0.min),
					   TREE_INT_CST_HIGH (vr0.min), 0, 0);
	  new_max = force_fit_type_double (outer_type,
					   TREE_INT_CST_LOW (vr0.max),
					   TREE_INT_CST_HIGH (vr0.max), 0, 0);
	  if (is_overflow_infinity (vr0.min))
	    new_min = negative_overflow_infinity (outer_type);
	  if (is_overflow_infinity (vr0.max))
	    new_max = positive_overflow_infinity (outer_type);
	  set_and_canonicalize_value_range (vr, vr0.type,
					    new_min, new_max, NULL);
	  return;
	}

      set_value_range_to_varying (vr);
      return;
    }

  /* Conversion of a VR_VARYING value to a wider type can result
     in a usable range.  So wait until after we've handled conversions
     before dropping the result to VR_VARYING if we had a source
     operand that is VR_VARYING.  */
  if (vr0.type == VR_VARYING)
    {
      set_value_range_to_varying (vr);
      return;
    }

  /* Apply the operation to each end of the range and see what we end
     up with.  */
  if (code == NEGATE_EXPR
      && !TYPE_UNSIGNED (type))
    {
      /* NEGATE_EXPR flips the range around.  We need to treat
	 TYPE_MIN_VALUE specially.  */
      if (is_positive_overflow_infinity (vr0.max))
	min = negative_overflow_infinity (type);
      else if (is_negative_overflow_infinity (vr0.max))
	min = positive_overflow_infinity (type);
      else if (!vrp_val_is_min (vr0.max))
	min = fold_unary_to_constant (code, type, vr0.max);
      else if (needs_overflow_infinity (type))
	{
	  if (supports_overflow_infinity (type)
	      && !is_overflow_infinity (vr0.min)
	      && !vrp_val_is_min (vr0.min))
	    min = positive_overflow_infinity (type);
	  else
	    {
	      set_value_range_to_varying (vr);
	      return;
	    }
	}
      else
	min = TYPE_MIN_VALUE (type);

      if (is_positive_overflow_infinity (vr0.min))
	max = negative_overflow_infinity (type);
      else if (is_negative_overflow_infinity (vr0.min))
	max = positive_overflow_infinity (type);
      else if (!vrp_val_is_min (vr0.min))
	max = fold_unary_to_constant (code, type, vr0.min);
      else if (needs_overflow_infinity (type))
	{
	  if (supports_overflow_infinity (type))
	    max = positive_overflow_infinity (type);
	  else
	    {
	      set_value_range_to_varying (vr);
	      return;
	    }
	}
      else
	max = TYPE_MIN_VALUE (type);
    }
  else if (code == NEGATE_EXPR
	   && TYPE_UNSIGNED (type))
    {
      if (!range_includes_zero_p (&vr0))
	{
	  max = fold_unary_to_constant (code, type, vr0.min);
	  min = fold_unary_to_constant (code, type, vr0.max);
	}
      else
	{
	  if (range_is_null (&vr0))
	    set_value_range_to_null (vr, type);
	  else
	    set_value_range_to_varying (vr);
	  return;
	}
    }
  else if (code == ABS_EXPR
           && !TYPE_UNSIGNED (type))
    {
      /* -TYPE_MIN_VALUE = TYPE_MIN_VALUE with flag_wrapv so we can't get a
         useful range.  */
      if (!TYPE_OVERFLOW_UNDEFINED (type)
	  && ((vr0.type == VR_RANGE
	       && vrp_val_is_min (vr0.min))
	      || (vr0.type == VR_ANTI_RANGE
		  && !vrp_val_is_min (vr0.min)
		  && !range_includes_zero_p (&vr0))))
	{
	  set_value_range_to_varying (vr);
	  return;
	}

      /* ABS_EXPR may flip the range around, if the original range
	 included negative values.  */
      if (is_overflow_infinity (vr0.min))
	min = positive_overflow_infinity (type);
      else if (!vrp_val_is_min (vr0.min))
	min = fold_unary_to_constant (code, type, vr0.min);
      else if (!needs_overflow_infinity (type))
	min = TYPE_MAX_VALUE (type);
      else if (supports_overflow_infinity (type))
	min = positive_overflow_infinity (type);
      else
	{
	  set_value_range_to_varying (vr);
	  return;
	}

      if (is_overflow_infinity (vr0.max))
	max = positive_overflow_infinity (type);
      else if (!vrp_val_is_min (vr0.max))
	max = fold_unary_to_constant (code, type, vr0.max);
      else if (!needs_overflow_infinity (type))
	max = TYPE_MAX_VALUE (type);
      else if (supports_overflow_infinity (type)
	       /* We shouldn't generate [+INF, +INF] as set_value_range
		  doesn't like this and ICEs.  */
	       && !is_positive_overflow_infinity (min))
	max = positive_overflow_infinity (type);
      else
	{
	  set_value_range_to_varying (vr);
	  return;
	}

      cmp = compare_values (min, max);

      /* If a VR_ANTI_RANGEs contains zero, then we have
	 ~[-INF, min(MIN, MAX)].  */
      if (vr0.type == VR_ANTI_RANGE)
	{
	  if (range_includes_zero_p (&vr0))
	    {
	      /* Take the lower of the two values.  */
	      if (cmp != 1)
		max = min;

	      /* Create ~[-INF, min (abs(MIN), abs(MAX))]
	         or ~[-INF + 1, min (abs(MIN), abs(MAX))] when
		 flag_wrapv is set and the original anti-range doesn't include
	         TYPE_MIN_VALUE, remember -TYPE_MIN_VALUE = TYPE_MIN_VALUE.  */
	      if (TYPE_OVERFLOW_WRAPS (type))
		{
		  tree type_min_value = TYPE_MIN_VALUE (type);

		  min = (vr0.min != type_min_value
			 ? int_const_binop (PLUS_EXPR, type_min_value,
					    integer_one_node, 0)
			 : type_min_value);
		}
	      else
		{
		  if (overflow_infinity_range_p (&vr0))
		    min = negative_overflow_infinity (type);
		  else
		    min = TYPE_MIN_VALUE (type);
		}
	    }
	  else
	    {
	      /* All else has failed, so create the range [0, INF], even for
	         flag_wrapv since TYPE_MIN_VALUE is in the original
	         anti-range.  */
	      vr0.type = VR_RANGE;
	      min = build_int_cst (type, 0);
	      if (needs_overflow_infinity (type))
		{
		  if (supports_overflow_infinity (type))
		    max = positive_overflow_infinity (type);
		  else
		    {
		      set_value_range_to_varying (vr);
		      return;
		    }
		}
	      else
		max = TYPE_MAX_VALUE (type);
	    }
	}

      /* If the range contains zero then we know that the minimum value in the
         range will be zero.  */
      else if (range_includes_zero_p (&vr0))
	{
	  if (cmp == 1)
	    max = min;
	  min = build_int_cst (type, 0);
	}
      else
	{
          /* If the range was reversed, swap MIN and MAX.  */
	  if (cmp == 1)
	    {
	      tree t = min;
	      min = max;
	      max = t;
	    }
	}
    }
  else
    {
      /* Otherwise, operate on each end of the range.  */
      min = fold_unary_to_constant (code, type, vr0.min);
      max = fold_unary_to_constant (code, type, vr0.max);

      if (needs_overflow_infinity (type))
	{
	  gcc_assert (code != NEGATE_EXPR && code != ABS_EXPR);

	  /* If both sides have overflowed, we don't know
	     anything.  */
	  if ((is_overflow_infinity (vr0.min)
	       || TREE_OVERFLOW (min))
	      && (is_overflow_infinity (vr0.max)
		  || TREE_OVERFLOW (max)))
	    {
	      set_value_range_to_varying (vr);
	      return;
	    }

	  if (is_overflow_infinity (vr0.min))
	    min = vr0.min;
	  else if (TREE_OVERFLOW (min))
	    {
	      if (supports_overflow_infinity (type))
		min = (tree_int_cst_sgn (min) >= 0
		       ? positive_overflow_infinity (TREE_TYPE (min))
		       : negative_overflow_infinity (TREE_TYPE (min)));
	      else
		{
		  set_value_range_to_varying (vr);
		  return;
		}
	    }

	  if (is_overflow_infinity (vr0.max))
	    max = vr0.max;
	  else if (TREE_OVERFLOW (max))
	    {
	      if (supports_overflow_infinity (type))
		max = (tree_int_cst_sgn (max) >= 0
		       ? positive_overflow_infinity (TREE_TYPE (max))
		       : negative_overflow_infinity (TREE_TYPE (max)));
	      else
		{
		  set_value_range_to_varying (vr);
		  return;
		}
	    }
	}
    }

  cmp = compare_values (min, max);
  if (cmp == -2 || cmp == 1)
    {
      /* If the new range has its limits swapped around (MIN > MAX),
	 then the operation caused one of them to wrap around, mark
	 the new range VARYING.  */
      set_value_range_to_varying (vr);
    }
  else
    set_value_range (vr, vr0.type, min, max, NULL);
}


/* Extract range information from a conditional expression EXPR based on
   the ranges of each of its operands and the expression code.  */

static void
extract_range_from_cond_expr (value_range_t *vr, tree expr)
{
  tree op0, op1;
  value_range_t vr0 = { VR_UNDEFINED, NULL_TREE, NULL_TREE, NULL };
  value_range_t vr1 = { VR_UNDEFINED, NULL_TREE, NULL_TREE, NULL };

  /* Get value ranges for each operand.  For constant operands, create
     a new value range with the operand to simplify processing.  */
  op0 = COND_EXPR_THEN (expr);
  if (TREE_CODE (op0) == SSA_NAME)
    vr0 = *(get_value_range (op0));
  else if (is_gimple_min_invariant (op0))
    set_value_range_to_value (&vr0, op0, NULL);
  else
    set_value_range_to_varying (&vr0);

  op1 = COND_EXPR_ELSE (expr);
  if (TREE_CODE (op1) == SSA_NAME)
    vr1 = *(get_value_range (op1));
  else if (is_gimple_min_invariant (op1))
    set_value_range_to_value (&vr1, op1, NULL);
  else
    set_value_range_to_varying (&vr1);

  /* The resulting value range is the union of the operand ranges */
  vrp_meet (&vr0, &vr1);
  copy_value_range (vr, &vr0);
}


/* Extract range information from a comparison expression EXPR based
   on the range of its operand and the expression code.  */

static void
extract_range_from_comparison (value_range_t *vr, enum tree_code code,
			       tree type, tree op0, tree op1)
{
  bool sop = false;
  tree val;

  val = vrp_evaluate_conditional_warnv_with_ops (code, op0, op1, false, &sop,
  						 NULL);

  /* A disadvantage of using a special infinity as an overflow
     representation is that we lose the ability to record overflow
     when we don't have an infinity.  So we have to ignore a result
     which relies on overflow.  */

  if (val && !is_overflow_infinity (val) && !sop)
    {
      /* Since this expression was found on the RHS of an assignment,
	 its type may be different from _Bool.  Convert VAL to EXPR's
	 type.  */
      val = fold_convert (type, val);
      if (is_gimple_min_invariant (val))
	set_value_range_to_value (vr, val, vr->equiv);
      else
	set_value_range (vr, VR_RANGE, val, val, vr->equiv);
    }
  else
    /* The result of a comparison is always true or false.  */
    set_value_range_to_truthvalue (vr, type);
}

/* Try to derive a nonnegative or nonzero range out of STMT relying
   primarily on generic routines in fold in conjunction with range data.
   Store the result in *VR */

static void
extract_range_basic (value_range_t *vr, GIMPLE_type stmt)
{
  bool sop = false;
  tree type = gimple_expr_type (stmt);

  if (INTEGRAL_TYPE_P (type)
      && gimple_stmt_nonnegative_warnv_p (stmt, &sop))
    set_value_range_to_nonnegative (vr, type,
				    sop || stmt_overflow_infinity (stmt));
  else if (vrp_stmt_computes_nonzero (stmt, &sop)
	   && !sop)
    set_value_range_to_nonnull (vr, type);
  else
    set_value_range_to_varying (vr);
}


/* Try to compute a useful range out of assignment STMT and store it
   in *VR.  */

static void
extract_range_from_assignment (value_range_t *vr, GIMPLE_type stmt)
{
  enum tree_code code = gimple_assign_rhs_code (stmt);

  if (code == ASSERT_EXPR)
    extract_range_from_assert (vr, gimple_assign_rhs1 (stmt));
  else if (code == SSA_NAME)
    extract_range_from_ssa_name (vr, gimple_assign_rhs1 (stmt));
  else if (TREE_CODE_CLASS (code) == tcc_binary
	   || code == TRUTH_AND_EXPR
	   || code == TRUTH_OR_EXPR
	   || code == TRUTH_XOR_EXPR)
    extract_range_from_binary_expr (vr, gimple_assign_rhs_code (stmt),
				    gimple_expr_type (stmt),
				    gimple_assign_rhs1 (stmt),
				    gimple_assign_rhs2 (stmt));
  else if (TREE_CODE_CLASS (code) == tcc_unary)
    extract_range_from_unary_expr (vr, gimple_assign_rhs_code (stmt),
				   gimple_expr_type (stmt),
				   gimple_assign_rhs1 (stmt));
  else if (code == COND_EXPR)
    extract_range_from_cond_expr (vr, gimple_assign_rhs1 (stmt));
  else if (TREE_CODE_CLASS (code) == tcc_comparison)
    extract_range_from_comparison (vr, gimple_assign_rhs_code (stmt),
				   gimple_expr_type (stmt),
				   gimple_assign_rhs1 (stmt),
				   gimple_assign_rhs2 (stmt));
  else if (get_gimple_rhs_class (code) == GIMPLE_SINGLE_RHS
	   && is_gimple_min_invariant (gimple_assign_rhs1 (stmt)))
    set_value_range_to_value (vr, gimple_assign_rhs1 (stmt), NULL);
  else
    set_value_range_to_varying (vr);

  if (vr->type == VR_VARYING)
    extract_range_basic (vr, stmt);
}

/* Given a range VR, a LOOP and a variable VAR, determine whether it
   would be profitable to adjust VR using scalar evolution information
   for VAR.  If so, update VR with the new limits.  */

static void
adjust_range_with_scev (value_range_t *vr, struct loop *loop,
         GIMPLE_type stmt, tree var)
{
  tree init, step, chrec, tmin, tmax, min, max, type, tem;
  enum ev_direction dir;

  /* TODO.  Don't adjust anti-ranges.  An anti-range may provide
     better opportunities than a regular range, but I'm not sure.  */
  if (vr->type == VR_ANTI_RANGE)
    return;

  chrec = instantiate_parameters (loop, analyze_scalar_evolution (loop, var));

  /* Like in PR19590, scev can return a constant function.  */
  if (is_gimple_min_invariant (chrec))
    {
      set_value_range_to_value (vr, chrec, vr->equiv);
      return;
    }

  if (TREE_CODE (chrec) != POLYNOMIAL_CHREC)
    return;

  init = initial_condition_in_loop_num (chrec, loop->num);
  tem = op_with_constant_singleton_value_range (init);
  if (tem)
    init = tem;
  step = evolution_part_in_loop_num (chrec, loop->num);
  tem = op_with_constant_singleton_value_range (step);
  if (tem)
    step = tem;

  /* If STEP is symbolic, we can't know whether INIT will be the
     minimum or maximum value in the range.  Also, unless INIT is
     a simple expression, compare_values and possibly other functions
     in tree-vrp won't be able to handle it.  */
  if (step == NULL_TREE
      || !is_gimple_min_invariant (step)
      || !valid_value_p (init))
    return;

  dir = scev_direction (chrec);
  if (/* Do not adjust ranges if we do not know whether the iv increases
	 or decreases,  ... */
      dir == EV_DIR_UNKNOWN
      /* ... or if it may wrap.  */
      || scev_probably_wraps_p (init, step, stmt, get_chrec_loop (chrec),
				true))
    return;

  /* We use TYPE_MIN_VALUE and TYPE_MAX_VALUE here instead of
     negative_overflow_infinity and positive_overflow_infinity,
     because we have concluded that the loop probably does not
     wrap.  */

  type = TREE_TYPE (var);
  if (POINTER_TYPE_P (type) || !TYPE_MIN_VALUE (type))
    tmin = lower_bound_in_type (type, type);
  else
    tmin = TYPE_MIN_VALUE (type);
  if (POINTER_TYPE_P (type) || !TYPE_MAX_VALUE (type))
    tmax = upper_bound_in_type (type, type);
  else
    tmax = TYPE_MAX_VALUE (type);

  if (vr->type == VR_VARYING || vr->type == VR_UNDEFINED)
    {
      min = tmin;
      max = tmax;

      /* For VARYING or UNDEFINED ranges, just about anything we get
	 from scalar evolutions should be better.  */

      if (dir == EV_DIR_DECREASES)
	max = init;
      else
	min = init;

      /* If we would create an invalid range, then just assume we
	 know absolutely nothing.  This may be over-conservative,
	 but it's clearly safe, and should happen only in unreachable
         parts of code, or for invalid programs.  */
      if (compare_values (min, max) == 1)
	return;

      set_value_range (vr, VR_RANGE, min, max, vr->equiv);
    }
  else if (vr->type == VR_RANGE)
    {
      min = vr->min;
      max = vr->max;

      if (dir == EV_DIR_DECREASES)
	{
	  /* INIT is the maximum value.  If INIT is lower than VR->MAX
	     but no smaller than VR->MIN, set VR->MAX to INIT.  */
	  if (compare_values (init, max) == -1)
	    {
	      max = init;

	      /* If we just created an invalid range with the minimum
		 greater than the maximum, we fail conservatively.
		 This should happen only in unreachable
		 parts of code, or for invalid programs.  */
	      if (compare_values (min, max) == 1)
		return;
	    }

	  /* According to the loop information, the variable does not
	     overflow.  If we think it does, probably because of an
	     overflow due to arithmetic on a different INF value,
	     reset now.  */
	  if (is_negative_overflow_infinity (min))
	    min = tmin;
	}
      else
	{
	  /* If INIT is bigger than VR->MIN, set VR->MIN to INIT.  */
	  if (compare_values (init, min) == 1)
	    {
	      min = init;

	      /* Again, avoid creating invalid range by failing.  */
	      if (compare_values (min, max) == 1)
		return;
	    }

	  if (is_positive_overflow_infinity (max))
	    max = tmax;
	}

      set_value_range (vr, VR_RANGE, min, max, vr->equiv);
    }
}

/* Return true if VAR may overflow at STMT.  This checks any available
   loop information to see if we can determine that VAR does not
   overflow.  */

static bool
vrp_var_may_overflow (tree var, GIMPLE_type stmt)
{
  struct loop *l;
  tree chrec, init, step;

  if (current_loops == NULL)
    return true;

  l = loop_containing_stmt (stmt);
  if (l == NULL
      || !loop_outer (l))
    return true;

  chrec = instantiate_parameters (l, analyze_scalar_evolution (l, var));
  if (TREE_CODE (chrec) != POLYNOMIAL_CHREC)
    return true;

  init = initial_condition_in_loop_num (chrec, l->num);
  step = evolution_part_in_loop_num (chrec, l->num);

  if (step == NULL_TREE
      || !is_gimple_min_invariant (step)
      || !valid_value_p (init))
    return true;

  /* If we get here, we know something useful about VAR based on the
     loop information.  If it wraps, it may overflow.  */

  if (scev_probably_wraps_p (init, step, stmt, get_chrec_loop (chrec),
			     true))
    return true;

  if (dump_file && (dump_flags & TDF_DETAILS) != 0)
    {
      print_generic_expr (dump_file, var, 0);
      fprintf (dump_file, ": loop information indicates does not overflow\n");
    }

  return false;
}


/* Given two numeric value ranges VR0, VR1 and a comparison code COMP:

   - Return BOOLEAN_TRUE_NODE if VR0 COMP VR1 always returns true for
     all the values in the ranges.

   - Return BOOLEAN_FALSE_NODE if the comparison always returns false.

   - Return NULL_TREE if it is not always possible to determine the
     value of the comparison.

   Also set *STRICT_OVERFLOW_P to indicate whether a range with an
   overflow infinity was used in the test.  */


static tree
compare_ranges (enum tree_code comp, value_range_t *vr0, value_range_t *vr1,
		bool *strict_overflow_p)
{
  /* VARYING or UNDEFINED ranges cannot be compared.  */
  if (vr0->type == VR_VARYING
      || vr0->type == VR_UNDEFINED
      || vr1->type == VR_VARYING
      || vr1->type == VR_UNDEFINED)
    return NULL_TREE;

  /* Anti-ranges need to be handled separately.  */
  if (vr0->type == VR_ANTI_RANGE || vr1->type == VR_ANTI_RANGE)
    {
      /* If both are anti-ranges, then we cannot compute any
	 comparison.  */
      if (vr0->type == VR_ANTI_RANGE && vr1->type == VR_ANTI_RANGE)
	return NULL_TREE;

      /* These comparisons are never statically computable.  */
      if (comp == GT_EXPR
	  || comp == GE_EXPR
	  || comp == LT_EXPR
	  || comp == LE_EXPR)
	return NULL_TREE;

      /* Equality can be computed only between a range and an
	 anti-range.  ~[VAL1, VAL2] == [VAL1, VAL2] is always false.  */
      if (vr0->type == VR_RANGE)
	{
	  /* To simplify processing, make VR0 the anti-range.  */
	  value_range_t *tmp = vr0;
	  vr0 = vr1;
	  vr1 = tmp;
	}

      gcc_assert (comp == NE_EXPR || comp == EQ_EXPR);

      if (compare_values_warnv (vr0->min, vr1->min, strict_overflow_p) == 0
	  && compare_values_warnv (vr0->max, vr1->max, strict_overflow_p) == 0)
	return (comp == NE_EXPR) ? boolean_true_node : boolean_false_node;

      return NULL_TREE;
    }

  if (!usable_range_p (vr0, strict_overflow_p)
      || !usable_range_p (vr1, strict_overflow_p))
    return NULL_TREE;

  /* Simplify processing.  If COMP is GT_EXPR or GE_EXPR, switch the
     operands around and change the comparison code.  */
  if (comp == GT_EXPR || comp == GE_EXPR)
    {
      value_range_t *tmp;
      comp = (comp == GT_EXPR) ? LT_EXPR : LE_EXPR;
      tmp = vr0;
      vr0 = vr1;
      vr1 = tmp;
    }

  if (comp == EQ_EXPR)
    {
      /* Equality may only be computed if both ranges represent
	 exactly one value.  */
      if (compare_values_warnv (vr0->min, vr0->max, strict_overflow_p) == 0
	  && compare_values_warnv (vr1->min, vr1->max, strict_overflow_p) == 0)
	{
	  int cmp_min = compare_values_warnv (vr0->min, vr1->min,
					      strict_overflow_p);
	  int cmp_max = compare_values_warnv (vr0->max, vr1->max,
					      strict_overflow_p);
	  if (cmp_min == 0 && cmp_max == 0)
	    return boolean_true_node;
	  else if (cmp_min != -2 && cmp_max != -2)
	    return boolean_false_node;
	}
      /* If [V0_MIN, V1_MAX] < [V1_MIN, V1_MAX] then V0 != V1.  */
      else if (compare_values_warnv (vr0->min, vr1->max,
				     strict_overflow_p) == 1
	       || compare_values_warnv (vr1->min, vr0->max,
					strict_overflow_p) == 1)
	return boolean_false_node;

      return NULL_TREE;
    }
  else if (comp == NE_EXPR)
    {
      int cmp1, cmp2;

      /* If VR0 is completely to the left or completely to the right
	 of VR1, they are always different.  Notice that we need to
	 make sure that both comparisons yield similar results to
	 avoid comparing values that cannot be compared at
	 compile-time.  */
      cmp1 = compare_values_warnv (vr0->max, vr1->min, strict_overflow_p);
      cmp2 = compare_values_warnv (vr0->min, vr1->max, strict_overflow_p);
      if ((cmp1 == -1 && cmp2 == -1) || (cmp1 == 1 && cmp2 == 1))
	return boolean_true_node;

      /* If VR0 and VR1 represent a single value and are identical,
	 return false.  */
      else if (compare_values_warnv (vr0->min, vr0->max,
				     strict_overflow_p) == 0
	       && compare_values_warnv (vr1->min, vr1->max,
					strict_overflow_p) == 0
	       && compare_values_warnv (vr0->min, vr1->min,
					strict_overflow_p) == 0
	       && compare_values_warnv (vr0->max, vr1->max,
					strict_overflow_p) == 0)
	return boolean_false_node;

      /* Otherwise, they may or may not be different.  */
      else
	return NULL_TREE;
    }
  else if (comp == LT_EXPR || comp == LE_EXPR)
    {
      int tst;

      /* If VR0 is to the left of VR1, return true.  */
      tst = compare_values_warnv (vr0->max, vr1->min, strict_overflow_p);
      if ((comp == LT_EXPR && tst == -1)
	  || (comp == LE_EXPR && (tst == -1 || tst == 0)))
	{
	  if (overflow_infinity_range_p (vr0)
	      || overflow_infinity_range_p (vr1))
	    *strict_overflow_p = true;
	  return boolean_true_node;
	}

      /* If VR0 is to the right of VR1, return false.  */
      tst = compare_values_warnv (vr0->min, vr1->max, strict_overflow_p);
      if ((comp == LT_EXPR && (tst == 0 || tst == 1))
	  || (comp == LE_EXPR && tst == 1))
	{
	  if (overflow_infinity_range_p (vr0)
	      || overflow_infinity_range_p (vr1))
	    *strict_overflow_p = true;
	  return boolean_false_node;
	}

      /* Otherwise, we don't know.  */
      return NULL_TREE;
    }

  gcc_unreachable ();
}


/* Given a value range VR, a value VAL and a comparison code COMP, return
   BOOLEAN_TRUE_NODE if VR COMP VAL always returns true for all the
   values in VR.  Return BOOLEAN_FALSE_NODE if the comparison
   always returns false.  Return NULL_TREE if it is not always
   possible to determine the value of the comparison.  Also set
   *STRICT_OVERFLOW_P to indicate whether a range with an overflow
   infinity was used in the test.  */

static tree
compare_range_with_value (enum tree_code comp, value_range_t *vr, tree val,
			  bool *strict_overflow_p)
{
  if (vr->type == VR_VARYING || vr->type == VR_UNDEFINED)
    return NULL_TREE;

  /* Anti-ranges need to be handled separately.  */
  if (vr->type == VR_ANTI_RANGE)
    {
      /* For anti-ranges, the only predicates that we can compute at
	 compile time are equality and inequality.  */
      if (comp == GT_EXPR
	  || comp == GE_EXPR
	  || comp == LT_EXPR
	  || comp == LE_EXPR)
	return NULL_TREE;

      /* ~[VAL_1, VAL_2] OP VAL is known if VAL_1 <= VAL <= VAL_2.  */
      if (value_inside_range (val, vr) == 1)
	return (comp == NE_EXPR) ? boolean_true_node : boolean_false_node;

      return NULL_TREE;
    }

  if (!usable_range_p (vr, strict_overflow_p))
    return NULL_TREE;

  if (comp == EQ_EXPR)
    {
      /* EQ_EXPR may only be computed if VR represents exactly
	 one value.  */
      if (compare_values_warnv (vr->min, vr->max, strict_overflow_p) == 0)
	{
	  int cmp = compare_values_warnv (vr->min, val, strict_overflow_p);
	  if (cmp == 0)
	    return boolean_true_node;
	  else if (cmp == -1 || cmp == 1 || cmp == 2)
	    return boolean_false_node;
	}
      else if (compare_values_warnv (val, vr->min, strict_overflow_p) == -1
	       || compare_values_warnv (vr->max, val, strict_overflow_p) == -1)
	return boolean_false_node;

      return NULL_TREE;
    }
  else if (comp == NE_EXPR)
    {
      /* If VAL is not inside VR, then they are always different.  */
      if (compare_values_warnv (vr->max, val, strict_overflow_p) == -1
	  || compare_values_warnv (vr->min, val, strict_overflow_p) == 1)
	return boolean_true_node;

      /* If VR represents exactly one value equal to VAL, then return
	 false.  */
      if (compare_values_warnv (vr->min, vr->max, strict_overflow_p) == 0
	  && compare_values_warnv (vr->min, val, strict_overflow_p) == 0)
	return boolean_false_node;

      /* Otherwise, they may or may not be different.  */
      return NULL_TREE;
    }
  else if (comp == LT_EXPR || comp == LE_EXPR)
    {
      int tst;

      /* If VR is to the left of VAL, return true.  */
      tst = compare_values_warnv (vr->max, val, strict_overflow_p);
      if ((comp == LT_EXPR && tst == -1)
	  || (comp == LE_EXPR && (tst == -1 || tst == 0)))
	{
	  if (overflow_infinity_range_p (vr))
	    *strict_overflow_p = true;
	  return boolean_true_node;
	}

      /* If VR is to the right of VAL, return false.  */
      tst = compare_values_warnv (vr->min, val, strict_overflow_p);
      if ((comp == LT_EXPR && (tst == 0 || tst == 1))
	  || (comp == LE_EXPR && tst == 1))
	{
	  if (overflow_infinity_range_p (vr))
	    *strict_overflow_p = true;
	  return boolean_false_node;
	}

      /* Otherwise, we don't know.  */
      return NULL_TREE;
    }
  else if (comp == GT_EXPR || comp == GE_EXPR)
    {
      int tst;

      /* If VR is to the right of VAL, return true.  */
      tst = compare_values_warnv (vr->min, val, strict_overflow_p);
      if ((comp == GT_EXPR && tst == 1)
	  || (comp == GE_EXPR && (tst == 0 || tst == 1)))
	{
	  if (overflow_infinity_range_p (vr))
	    *strict_overflow_p = true;
	  return boolean_true_node;
	}

      /* If VR is to the left of VAL, return false.  */
      tst = compare_values_warnv (vr->max, val, strict_overflow_p);
      if ((comp == GT_EXPR && (tst == -1 || tst == 0))
	  || (comp == GE_EXPR && tst == -1))
	{
	  if (overflow_infinity_range_p (vr))
	    *strict_overflow_p = true;
	  return boolean_false_node;
	}

      /* Otherwise, we don't know.  */
      return NULL_TREE;
    }

  gcc_unreachable ();
}


/* Debugging dumps.  */

static void dump_value_range (FILE *, value_range_t *);
static void debug_value_range (value_range_t *);
static void dump_all_value_ranges (FILE *);
static void debug_all_value_ranges (void);
static void dump_vr_equiv (FILE *, bitmap);
static void debug_vr_equiv (bitmap);


/* Dump value range VR to FILE.  */

static void
dump_value_range (FILE *file, value_range_t *vr)
{
  if (vr == NULL)
    fprintf (file, "[]");
  else if (vr->type == VR_UNDEFINED)
    fprintf (file, "UNDEFINED");
  else if (vr->type == VR_RANGE || vr->type == VR_ANTI_RANGE)
    {
      tree type = TREE_TYPE (vr->min);

      fprintf (file, "%s[", (vr->type == VR_ANTI_RANGE) ? "~" : "");

      if (is_negative_overflow_infinity (vr->min))
	fprintf (file, "-INF(OVF)");
      else if (INTEGRAL_TYPE_P (type)
	       && !TYPE_UNSIGNED (type)
	       && vrp_val_is_min (vr->min))
	fprintf (file, "-INF");
      else
	print_generic_expr (file, vr->min, 0);

      fprintf (file, ", ");

      if (is_positive_overflow_infinity (vr->max))
	fprintf (file, "+INF(OVF)");
      else if (INTEGRAL_TYPE_P (type)
	       && vrp_val_is_max (vr->max))
	fprintf (file, "+INF");
      else
	print_generic_expr (file, vr->max, 0);

      fprintf (file, "]");

      if (vr->equiv)
	{
	  bitmap_iterator bi;
	  unsigned i, c = 0;

	  fprintf (file, "  EQUIVALENCES: { ");

	  EXECUTE_IF_SET_IN_BITMAP (vr->equiv, 0, i, bi)
	    {
	      print_generic_expr (file, ssa_name (i), 0);
	      fprintf (file, " ");
	      c++;
	    }

	  fprintf (file, "} (%u elements)", c);
	}
    }
  else if (vr->type == VR_VARYING)
    fprintf (file, "VARYING");
  else
    fprintf (file, "INVALID RANGE");
}


/* Dump value range VR to stderr.  */

static void
debug_value_range (value_range_t *vr)
{
  dump_value_range (stderr, vr);
  fprintf (stderr, "\n");
}


/* Dump value ranges of all SSA_NAMEs to FILE.  */

static void
dump_all_value_ranges (FILE *file)
{
  size_t i;

  for (i = 0; i < num_ssa_names; i++)
    {
      if (vr_value[i])
	{
	  print_generic_expr (file, ssa_name (i), 0);
	  fprintf (file, ": ");
	  dump_value_range (file, vr_value[i]);
	  fprintf (file, "\n");
	}
    }

  fprintf (file, "\n");
}


/* Dump all value ranges to stderr.  */

static void
debug_all_value_ranges (void)
{
  dump_all_value_ranges (stderr);
}


/* Given a COND_EXPR COND of the form 'V OP W', and an SSA name V,
   create a new SSA name N and return the assertion assignment
   'V = ASSERT_EXPR <V, V OP W>'.  */

static GIMPLE_type
build_assert_expr_for (tree cond, tree v)
{
  tree n;
  GIMPLE_type assertion;

  gcc_assert (TREE_CODE (v) == SSA_NAME);
  n = duplicate_ssa_name (v, NULL);

  if (COMPARISON_CLASS_P (cond))
    {
      tree a = build2 (ASSERT_EXPR, TREE_TYPE (v), v, cond);
      assertion = gimple_build_assign (n, a);
    }
  else if (TREE_CODE (cond) == TRUTH_NOT_EXPR)
    {
      /* Given !V, build the assignment N = false.  */
      tree op0 = TREE_OPERAND (cond, 0);
      gcc_assert (op0 == v);
      assertion = gimple_build_assign (n, boolean_false_node);
    }
  else if (TREE_CODE (cond) == SSA_NAME)
    {
      /* Given V, build the assignment N = true.  */
      gcc_assert (v == cond);
      assertion = gimple_build_assign (n, boolean_true_node);
    }
  else
    gcc_unreachable ();

  SSA_NAME_DEF_STMT (n) = assertion;

  /* The new ASSERT_EXPR, creates a new SSA name that replaces the
     operand of the ASSERT_EXPR. Register the new name and the old one
     in the replacement table so that we can fix the SSA web after
     adding all the ASSERT_EXPRs.  */
  register_new_name_mapping (n, v);

  return assertion;
}


/* Return false if EXPR is a predicate expression involving floating
   point values.  */

static inline bool
fp_predicate (GIMPLE_type stmt)
{
  GIMPLE_CHECK (stmt, GIMPLE_COND);

  return FLOAT_TYPE_P (TREE_TYPE (gimple_cond_lhs (stmt)));
}


/* If the range of values taken by OP can be inferred after STMT executes,
   return the comparison code (COMP_CODE_P) and value (VAL_P) that
   describes the inferred range.  Return true if a range could be
   inferred.  */

static bool
infer_value_range (GIMPLE_type stmt, tree op, enum tree_code *comp_code_p, tree *val_p)
{
  *val_p = NULL_TREE;
  *comp_code_p = ERROR_MARK;

  /* Do not attempt to infer anything in names that flow through
     abnormal edges.  */
  if (SSA_NAME_OCCURS_IN_ABNORMAL_PHI (op))
    return false;

  /* Similarly, don't infer anything from statements that may throw
     exceptions.  */
  if (stmt_could_throw_p (stmt))
    return false;

  /* If STMT is the last statement of a basic block with no
     successors, there is no point inferring anything about any of its
     operands.  We would not be able to find a proper insertion point
     for the assertion, anyway.  */
  if (stmt_ends_bb_p (stmt) && EDGE_COUNT (gimple_bb (stmt)->succs) == 0)
    return false;

  /* We can only assume that a pointer dereference will yield
     non-NULL if -fdelete-null-pointer-checks is enabled.  */
  if (flag_delete_null_pointer_checks
      && POINTER_TYPE_P (TREE_TYPE (op))
      && gimple_code (stmt) != GIMPLE_ASM)
    {
      unsigned num_uses, num_loads, num_stores;

      count_uses_and_derefs (op, stmt, &num_uses, &num_loads, &num_stores);
      if (num_loads + num_stores > 0)
	{
	  *val_p = build_int_cst (TREE_TYPE (op), 0);
	  *comp_code_p = NE_EXPR;
	  return true;
	}
    }

  return false;
}


static void dump_asserts_for (FILE *, tree);
static void debug_asserts_for (tree);
static void dump_all_asserts (FILE *);
static void debug_all_asserts (void);

/* Dump all the registered assertions for NAME to FILE.  */

static void
dump_asserts_for (FILE *file, tree name)
{
  assert_locus_t loc;

  fprintf (file, "Assertions to be inserted for ");
  print_generic_expr (file, name, 0);
  fprintf (file, "\n");

  loc = asserts_for[SSA_NAME_VERSION (name)];
  while (loc)
    {
      fprintf (file, "\t");
      print_gimple_stmt (file, gsi_stmt (loc->si), 0, 0);
      fprintf (file, "\n\tBB #%d", loc->bb->index);
      if (loc->e)
	{
	  fprintf (file, "\n\tEDGE %d->%d", loc->e->src->index,
	           loc->e->dest->index);
	  dump_edge_info (file, loc->e, 0);
	}
      fprintf (file, "\n\tPREDICATE: ");
      print_generic_expr (file, name, 0);
      fprintf (file, " %s ", GET_TREE_CODE_NAME(loc->comp_code));
      print_generic_expr (file, loc->val, 0);
      fprintf (file, "\n\n");
      loc = loc->next;
    }

  fprintf (file, "\n");
}


/* Dump all the registered assertions for NAME to stderr.  */

static void
debug_asserts_for (tree name)
{
  dump_asserts_for (stderr, name);
}


/* Dump all the registered assertions for all the names to FILE.  */

static void
dump_all_asserts (FILE *file)
{
  unsigned i;
  bitmap_iterator bi;

  fprintf (file, "\nASSERT_EXPRs to be inserted\n\n");
  EXECUTE_IF_SET_IN_BITMAP (need_assert_for, 0, i, bi)
    dump_asserts_for (file, ssa_name (i));
  fprintf (file, "\n");
}


/* Dump all the registered assertions for all the names to stderr.  */

static void
debug_all_asserts (void)
{
  dump_all_asserts (stderr);
}


/* If NAME doesn't have an ASSERT_EXPR registered for asserting
   'EXPR COMP_CODE VAL' at a location that dominates block BB or
   E->DEST, then register this location as a possible insertion point
   for ASSERT_EXPR <NAME, EXPR COMP_CODE VAL>.

   BB, E and SI provide the exact insertion point for the new
   ASSERT_EXPR.  If BB is NULL, then the ASSERT_EXPR is to be inserted
   on edge E.  Otherwise, if E is NULL, the ASSERT_EXPR is inserted on
   BB.  If SI points to a COND_EXPR or a SWITCH_EXPR statement, then E
   must not be NULL.  */

static void
register_new_assert_for (tree name, tree expr,
			 enum tree_code comp_code,
			 tree val,
			 basic_block bb,
			 edge e,
			 gimple_stmt_iterator si)
{
  assert_locus_t n, loc, last_loc;
  basic_block dest_bb;

#if defined ENABLE_CHECKING
  gcc_assert (bb == NULL || e == NULL);

  if (e == NULL)
    gcc_assert (gimple_code (gsi_stmt (si)) != GIMPLE_COND
		&& gimple_code (gsi_stmt (si)) != GIMPLE_SWITCH);
#endif

  /* Never build an assert comparing against an integer constant with
     TREE_OVERFLOW set.  This confuses our undefined overflow warning
     machinery.  */
  if (TREE_CODE (val) == INTEGER_CST
      && TREE_OVERFLOW (val))
    val = build_int_cst_wide (TREE_TYPE (val),
			      TREE_INT_CST_LOW (val), TREE_INT_CST_HIGH (val));

  /* The new assertion A will be inserted at BB or E.  We need to
     determine if the new location is dominated by a previously
     registered location for A.  If we are doing an edge insertion,
     assume that A will be inserted at E->DEST.  Note that this is not
     necessarily true.

     If E is a critical edge, it will be split.  But even if E is
     split, the new block will dominate the same set of blocks that
     E->DEST dominates.

     The reverse, however, is not true, blocks dominated by E->DEST
     will not be dominated by the new block created to split E.  So,
     if the insertion location is on a critical edge, we will not use
     the new location to move another assertion previously registered
     at a block dominated by E->DEST.  */
  dest_bb = (bb) ? bb : e->dest;

  /* If NAME already has an ASSERT_EXPR registered for COMP_CODE and
     VAL at a block dominating DEST_BB, then we don't need to insert a new
     one.  Similarly, if the same assertion already exists at a block
     dominated by DEST_BB and the new location is not on a critical
     edge, then update the existing location for the assertion (i.e.,
     move the assertion up in the dominance tree).

     Note, this is implemented as a simple linked list because there
     should not be more than a handful of assertions registered per
     name.  If this becomes a performance problem, a table hashed by
     COMP_CODE and VAL could be implemented.  */
  loc = asserts_for[SSA_NAME_VERSION (name)];
  last_loc = loc;
  while (loc)
    {
      if (loc->comp_code == comp_code
	  && (loc->val == val
	      || operand_equal_p (loc->val, val, 0))
	  && (loc->expr == expr
	      || operand_equal_p (loc->expr, expr, 0)))
	{
	  /* If the assertion NAME COMP_CODE VAL has already been
	     registered at a basic block that dominates DEST_BB, then
	     we don't need to insert the same assertion again.  Note
	     that we don't check strict dominance here to avoid
	     replicating the same assertion inside the same basic
	     block more than once (e.g., when a pointer is
	     dereferenced several times inside a block).

	     An exception to this rule are edge insertions.  If the
	     new assertion is to be inserted on edge E, then it will
	     dominate all the other insertions that we may want to
	     insert in DEST_BB.  So, if we are doing an edge
	     insertion, don't do this dominance check.  */
          if (e == NULL
	      && dominated_by_p (CDI_DOMINATORS, dest_bb, loc->bb))
	    return;

	  /* Otherwise, if E is not a critical edge and DEST_BB
	     dominates the existing location for the assertion, move
	     the assertion up in the dominance tree by updating its
	     location information.  */
	  if ((e == NULL || !EDGE_CRITICAL_P (e))
	      && dominated_by_p (CDI_DOMINATORS, loc->bb, dest_bb))
	    {
	      loc->bb = dest_bb;
	      loc->e = e;
	      loc->si = si;
	      return;
	    }
	}

      /* Update the last node of the list and move to the next one.  */
      last_loc = loc;
      loc = loc->next;
    }

  /* If we didn't find an assertion already registered for
     NAME COMP_CODE VAL, add a new one at the end of the list of
     assertions associated with NAME.  */
  n = XNEW (struct assert_locus_d);
  n->bb = dest_bb;
  n->e = e;
  n->si = si;
  n->comp_code = comp_code;
  n->val = val;
  n->expr = expr;
  n->next = NULL;

  if (last_loc)
    last_loc->next = n;
  else
    asserts_for[SSA_NAME_VERSION (name)] = n;

  bitmap_set_bit (need_assert_for, SSA_NAME_VERSION (name));
}

/* (COND_OP0 COND_CODE COND_OP1) is a predicate which uses NAME.
   Extract a suitable test code and value and store them into *CODE_P and
   *VAL_P so the predicate is normalized to NAME *CODE_P *VAL_P.

   If no extraction was possible, return FALSE, otherwise return TRUE.

   If INVERT is true, then we invert the result stored into *CODE_P.  */

static bool
extract_code_and_val_from_cond_with_ops (tree name, enum tree_code cond_code,
					 tree cond_op0, tree cond_op1,
					 bool invert, enum tree_code *code_p,
					 tree *val_p)
{
  enum tree_code comp_code;
  tree val;

  /* Otherwise, we have a comparison of the form NAME COMP VAL
     or VAL COMP NAME.  */
  if (name == cond_op1)
    {
      /* If the predicate is of the form VAL COMP NAME, flip
	 COMP around because we need to register NAME as the
	 first operand in the predicate.  */
      comp_code = swap_tree_comparison (cond_code);
      val = cond_op0;
    }
  else
    {
      /* The comparison is of the form NAME COMP VAL, so the
	 comparison code remains unchanged.  */
      comp_code = cond_code;
      val = cond_op1;
    }

  /* Invert the comparison code as necessary.  */
  if (invert)
    comp_code = invert_tree_comparison (comp_code, 0);

  /* VRP does not handle float types.  */
  if (SCALAR_FLOAT_TYPE_P (TREE_TYPE (val)))
    return false;

  /* Do not register always-false predicates.
     FIXME:  this works around a limitation in fold() when dealing with
     enumerations.  Given 'enum { N1, N2 } x;', fold will not
     fold 'if (x > N2)' to 'if (0)'.  */
  if ((comp_code == GT_EXPR || comp_code == LT_EXPR)
      && INTEGRAL_TYPE_P (TREE_TYPE (val)))
    {
      tree min = TYPE_MIN_VALUE (TREE_TYPE (val));
      tree max = TYPE_MAX_VALUE (TREE_TYPE (val));

      if (comp_code == GT_EXPR
	  && (!max
	      || compare_values (val, max) == 0))
	return false;

      if (comp_code == LT_EXPR
	  && (!min
	      || compare_values (val, min) == 0))
	return false;
    }
  *code_p = comp_code;
  *val_p = val;
  return true;
}

/* Try to register an edge assertion for SSA name NAME on edge E for
   the condition COND contributing to the conditional jump pointed to by BSI.
   Invert the condition COND if INVERT is true.
   Return true if an assertion for NAME could be registered.  */

static bool
register_edge_assert_for_2 (tree name, edge e, gimple_stmt_iterator bsi,
			    enum tree_code cond_code,
			    tree cond_op0, tree cond_op1, bool invert)
{
  tree val;
  enum tree_code comp_code;
  bool retval = false;

  if (!extract_code_and_val_from_cond_with_ops (name, cond_code,
						cond_op0,
						cond_op1,
						invert, &comp_code, &val))
    return false;

  /* Only register an ASSERT_EXPR if NAME was found in the sub-graph
     reachable from E.  */
  if (live_on_edge (e, name)
      && !has_single_use (name))
    {
      register_new_assert_for (name, name, comp_code, val, NULL, e, bsi);
      retval = true;
    }

  /* In the case of NAME <= CST and NAME being defined as
     NAME = (unsigned) NAME2 + CST2 we can assert NAME2 >= -CST2
     and NAME2 <= CST - CST2.  We can do the same for NAME > CST.
     This catches range and anti-range tests.  */
  if ((comp_code == LE_EXPR
       || comp_code == GT_EXPR)
      && TREE_CODE (val) == INTEGER_CST
      && TYPE_UNSIGNED (TREE_TYPE (val)))
    {
      GIMPLE_type def_stmt = SSA_NAME_DEF_STMT (name);
      tree cst2 = NULL_TREE, name2 = NULL_TREE, name3 = NULL_TREE;

      /* Extract CST2 from the (optional) addition.  */
      if (is_gimple_assign (def_stmt)
	  && gimple_assign_rhs_code (def_stmt) == PLUS_EXPR)
	{
	  name2 = gimple_assign_rhs1 (def_stmt);
	  cst2 = gimple_assign_rhs2 (def_stmt);
	  if (TREE_CODE (name2) == SSA_NAME
	      && TREE_CODE (cst2) == INTEGER_CST)
	    def_stmt = SSA_NAME_DEF_STMT (name2);
	}

      /* Extract NAME2 from the (optional) sign-changing cast.  */
      if (gimple_assign_cast_p (def_stmt))
	{
	  if (CONVERT_EXPR_CODE_P (gimple_assign_rhs_code (def_stmt))
	      && ! TYPE_UNSIGNED (TREE_TYPE (gimple_assign_rhs1 (def_stmt)))
	      && (TYPE_PRECISION (gimple_expr_type (def_stmt))
		  == TYPE_PRECISION (TREE_TYPE (gimple_assign_rhs1 (def_stmt)))))
	    name3 = gimple_assign_rhs1 (def_stmt);
	}

      /* If name3 is used later, create an ASSERT_EXPR for it.  */
      if (name3 != NULL_TREE
      	  && TREE_CODE (name3) == SSA_NAME
	  && (cst2 == NULL_TREE
	      || TREE_CODE (cst2) == INTEGER_CST)
	  && INTEGRAL_TYPE_P (TREE_TYPE (name3))
	  && live_on_edge (e, name3)
	  && !has_single_use (name3))
	{
	  tree tmp;

	  /* Build an expression for the range test.  */
	  tmp = build1 (NOP_EXPR, TREE_TYPE (name), name3);
	  if (cst2 != NULL_TREE)
	    tmp = build2 (PLUS_EXPR, TREE_TYPE (name), tmp, cst2);

	  if (dump_file)
	    {
	      fprintf (dump_file, "Adding assert for ");
	      print_generic_expr (dump_file, name3, 0);
	      fprintf (dump_file, " from ");
	      print_generic_expr (dump_file, tmp, 0);
	      fprintf (dump_file, "\n");
	    }

	  register_new_assert_for (name3, tmp, comp_code, val, NULL, e, bsi);

	  retval = true;
	}

      /* If name2 is used later, create an ASSERT_EXPR for it.  */
      if (name2 != NULL_TREE
      	  && TREE_CODE (name2) == SSA_NAME
	  && TREE_CODE (cst2) == INTEGER_CST
	  && INTEGRAL_TYPE_P (TREE_TYPE (name2))
	  && live_on_edge (e, name2)
	  && !has_single_use (name2))
	{
	  tree tmp;

	  /* Build an expression for the range test.  */
	  tmp = name2;
	  if (TREE_TYPE (name) != TREE_TYPE (name2))
	    tmp = build1 (NOP_EXPR, TREE_TYPE (name), tmp);
	  if (cst2 != NULL_TREE)
	    tmp = build2 (PLUS_EXPR, TREE_TYPE (name), tmp, cst2);

	  if (dump_file)
	    {
	      fprintf (dump_file, "Adding assert for ");
	      print_generic_expr (dump_file, name2, 0);
	      fprintf (dump_file, " from ");
	      print_generic_expr (dump_file, tmp, 0);
	      fprintf (dump_file, "\n");
	    }

	  register_new_assert_for (name2, tmp, comp_code, val, NULL, e, bsi);

	  retval = true;
	}
    }

  return retval;
}

/* OP is an operand of a truth value expression which is known to have
   a particular value.  Register any asserts for OP and for any
   operands in OP's defining statement.

   If CODE is EQ_EXPR, then we want to register OP is zero (false),
   if CODE is NE_EXPR, then we want to register OP is nonzero (true).   */

static bool
register_edge_assert_for_1 (tree op, enum tree_code code,
			    edge e, gimple_stmt_iterator bsi)
{
  bool retval = false;
  GIMPLE_type op_def;
  tree val;
  enum tree_code rhs_code;

  /* We only care about SSA_NAMEs.  */
  if (TREE_CODE (op) != SSA_NAME)
    return false;

  /* We know that OP will have a zero or nonzero value.  If OP is used
     more than once go ahead and register an assert for OP.

     The FOUND_IN_SUBGRAPH support is not helpful in this situation as
     it will always be set for OP (because OP is used in a COND_EXPR in
     the subgraph).  */
  if (!has_single_use (op))
    {
      val = build_int_cst (TREE_TYPE (op), 0);
      register_new_assert_for (op, op, code, val, NULL, e, bsi);
      retval = true;
    }

  /* Now look at how OP is set.  If it's set from a comparison,
     a truth operation or some bit operations, then we may be able
     to register information about the operands of that assignment.  */
  op_def = SSA_NAME_DEF_STMT (op);
  if (gimple_code (op_def) != GIMPLE_ASSIGN)
    return retval;

  rhs_code = gimple_assign_rhs_code (op_def);

  if (TREE_CODE_CLASS (rhs_code) == tcc_comparison)
    {
      bool invert = (code == EQ_EXPR ? true : false);
      tree op0 = gimple_assign_rhs1 (op_def);
      tree op1 = gimple_assign_rhs2 (op_def);

      if (TREE_CODE (op0) == SSA_NAME)
        retval |= register_edge_assert_for_2 (op0, e, bsi, rhs_code, op0, op1,
					      invert);
      if (TREE_CODE (op1) == SSA_NAME)
        retval |= register_edge_assert_for_2 (op1, e, bsi, rhs_code, op0, op1,
					      invert);
    }
  else if ((code == NE_EXPR
	    && (gimple_assign_rhs_code (op_def) == TRUTH_AND_EXPR
		|| gimple_assign_rhs_code (op_def) == BIT_AND_EXPR))
	   || (code == EQ_EXPR
	       && (gimple_assign_rhs_code (op_def) == TRUTH_OR_EXPR
		   || gimple_assign_rhs_code (op_def) == BIT_IOR_EXPR)))
    {
      /* Recurse on each operand.  */
      retval |= register_edge_assert_for_1 (gimple_assign_rhs1 (op_def),
					    code, e, bsi);
      retval |= register_edge_assert_for_1 (gimple_assign_rhs2 (op_def),
					    code, e, bsi);
    }
  else if (gimple_assign_rhs_code (op_def) == TRUTH_NOT_EXPR)
    {
      /* Recurse, flipping CODE.  */
      code = invert_tree_comparison (code, false);
      retval |= register_edge_assert_for_1 (gimple_assign_rhs1 (op_def),
					    code, e, bsi);
    }
  else if (gimple_assign_rhs_code (op_def) == SSA_NAME)
    {
      /* Recurse through the copy.  */
      retval |= register_edge_assert_for_1 (gimple_assign_rhs1 (op_def),
					    code, e, bsi);
    }
  else if (CONVERT_EXPR_CODE_P (gimple_assign_rhs_code (op_def)))
    {
      /* Recurse through the type conversion.  */
      retval |= register_edge_assert_for_1 (gimple_assign_rhs1 (op_def),
					    code, e, bsi);
    }

  return retval;
}

/* Try to register an edge assertion for SSA name NAME on edge E for
   the condition COND contributing to the conditional jump pointed to by SI.
   Return true if an assertion for NAME could be registered.  */

static bool
register_edge_assert_for (tree name, edge e, gimple_stmt_iterator si,
			  enum tree_code cond_code, tree cond_op0,
			  tree cond_op1)
{
  tree val;
  enum tree_code comp_code;
  bool retval = false;
  bool is_else_edge = (e->flags & EDGE_FALSE_VALUE) != 0;

  /* Do not attempt to infer anything in names that flow through
     abnormal edges.  */
  if (SSA_NAME_OCCURS_IN_ABNORMAL_PHI (name))
    return false;

  if (!extract_code_and_val_from_cond_with_ops (name, cond_code,
						cond_op0, cond_op1,
						is_else_edge,
						&comp_code, &val))
    return false;

  /* Register ASSERT_EXPRs for name.  */
  retval |= register_edge_assert_for_2 (name, e, si, cond_code, cond_op0,
					cond_op1, is_else_edge);


  /* If COND is effectively an equality test of an SSA_NAME against
     the value zero or one, then we may be able to assert values
     for SSA_NAMEs which flow into COND.  */

  /* In the case of NAME == 1 or NAME != 0, for TRUTH_AND_EXPR defining
     statement of NAME we can assert both operands of the TRUTH_AND_EXPR
     have nonzero value.  */
  if (((comp_code == EQ_EXPR && integer_onep (val))
       || (comp_code == NE_EXPR && integer_zerop (val))))
    {
      GIMPLE_type def_stmt = SSA_NAME_DEF_STMT (name);

      if (is_gimple_assign (def_stmt)
	  && (gimple_assign_rhs_code (def_stmt) == TRUTH_AND_EXPR
	      || gimple_assign_rhs_code (def_stmt) == BIT_AND_EXPR))
	{
	  tree op0 = gimple_assign_rhs1 (def_stmt);
	  tree op1 = gimple_assign_rhs2 (def_stmt);
	  retval |= register_edge_assert_for_1 (op0, NE_EXPR, e, si);
	  retval |= register_edge_assert_for_1 (op1, NE_EXPR, e, si);
	}
    }

  /* In the case of NAME == 0 or NAME != 1, for TRUTH_OR_EXPR defining
     statement of NAME we can assert both operands of the TRUTH_OR_EXPR
     have zero value.  */
  if (((comp_code == EQ_EXPR && integer_zerop (val))
       || (comp_code == NE_EXPR && integer_onep (val))))
    {
      GIMPLE_type def_stmt = SSA_NAME_DEF_STMT (name);

      if (is_gimple_assign (def_stmt)
	  && (gimple_assign_rhs_code (def_stmt) == TRUTH_OR_EXPR
	      /* For BIT_IOR_EXPR only if NAME == 0 both operands have
		 necessarily zero value.  */
	      || (comp_code == EQ_EXPR
		  && (gimple_assign_rhs_code (def_stmt) == BIT_IOR_EXPR))))
	{
	  tree op0 = gimple_assign_rhs1 (def_stmt);
	  tree op1 = gimple_assign_rhs2 (def_stmt);
	  retval |= register_edge_assert_for_1 (op0, EQ_EXPR, e, si);
	  retval |= register_edge_assert_for_1 (op1, EQ_EXPR, e, si);
	}
    }

  return retval;
}


/* Determine whether the outgoing edges of BB should receive an
   ASSERT_EXPR for each of the operands of BB's LAST statement.
   The last statement of BB must be a COND_EXPR.

   If any of the sub-graphs rooted at BB have an interesting use of
   the predicate operands, an assert location node is added to the
   list of assertions for the corresponding operands.  */

static bool
find_conditional_asserts (basic_block bb, GIMPLE_type last)
{
  bool need_assert;
  gimple_stmt_iterator bsi;
  tree op;
  edge_iterator ei;
  edge e;
  ssa_op_iter iter;

  need_assert = false;
  bsi = gsi_for_stmt (last);

  /* Look for uses of the operands in each of the sub-graphs
     rooted at BB.  We need to check each of the outgoing edges
     separately, so that we know what kind of ASSERT_EXPR to
     insert.  */
  FOR_EACH_EDGE (e, ei, bb->succs)
    {
      if (e->dest == bb)
	continue;

      /* Register the necessary assertions for each operand in the
	 conditional predicate.  */
      FOR_EACH_SSA_TREE_OPERAND (op, last, iter, SSA_OP_USE)
	{
	  need_assert |= register_edge_assert_for (op, e, bsi,
						   gimple_cond_code (last),
						   gimple_cond_lhs (last),
						   gimple_cond_rhs (last));
	}
    }

  return need_assert;
}

/* Compare two case labels sorting first by the destination label uid
   and then by the case value.  */

static int
compare_case_labels (const void *p1, const void *p2)
{
  const_tree const case1 = *(const_tree const*)p1;
  const_tree const case2 = *(const_tree const*)p2;
  unsigned int uid1 = DECL_UID (CASE_LABEL (case1));
  unsigned int uid2 = DECL_UID (CASE_LABEL (case2));

  if (uid1 < uid2)
    return -1;
  else if (uid1 == uid2)
    {
      /* Make sure the default label is first in a group.  */
      if (!CASE_LOW (case1))
	return -1;
      else if (!CASE_LOW (case2))
	return 1;
      else
        return tree_int_cst_compare (CASE_LOW (case1), CASE_LOW (case2));
    }
  else
    return 1;
}

/* Determine whether the outgoing edges of BB should receive an
   ASSERT_EXPR for each of the operands of BB's LAST statement.
   The last statement of BB must be a SWITCH_EXPR.

   If any of the sub-graphs rooted at BB have an interesting use of
   the predicate operands, an assert location node is added to the
   list of assertions for the corresponding operands.  */

static bool
find_switch_asserts (basic_block bb, GIMPLE_type last)
{
  bool need_assert;
  gimple_stmt_iterator bsi;
  tree op;
  edge e;
  tree vec2;
  size_t n = gimple_switch_num_labels(last);
#if GCC_VERSION >= 4000
  unsigned int idx;
#else
  /* Work around GCC 3.4 bug (PR 37086).  */
  volatile unsigned int idx;
#endif

  need_assert = false;
  bsi = gsi_for_stmt (last);
  op = gimple_switch_index (last);
  if (TREE_CODE (op) != SSA_NAME)
    return false;

  /* Build a vector of case labels sorted by destination label.  */
  vec2 = make_tree_vec (n);
  for (idx = 0; idx < n; ++idx)
    TREE_VEC_ELT (vec2, idx) = gimple_switch_label (last, idx);
  qsort (&TREE_VEC_ELT (vec2, 0), n, sizeof (tree), compare_case_labels);

  for (idx = 0; idx < n; ++idx)
    {
      tree min, max;
      tree cl = TREE_VEC_ELT (vec2, idx);

      min = CASE_LOW (cl);
      max = CASE_HIGH (cl);

      /* If there are multiple case labels with the same destination
	 we need to combine them to a single value range for the edge.  */
      if (idx + 1 < n
	  && CASE_LABEL (cl) == CASE_LABEL (TREE_VEC_ELT (vec2, idx + 1)))
	{
	  /* Skip labels until the last of the group.  */
	  do {
	    ++idx;
	  } while (idx < n
		   && CASE_LABEL (cl) == CASE_LABEL (TREE_VEC_ELT (vec2, idx)));
	  --idx;

	  /* Pick up the maximum of the case label range.  */
	  if (CASE_HIGH (TREE_VEC_ELT (vec2, idx)))
	    max = CASE_HIGH (TREE_VEC_ELT (vec2, idx));
	  else
	    max = CASE_LOW (TREE_VEC_ELT (vec2, idx));
	}

      /* Nothing to do if the range includes the default label until we
	 can register anti-ranges.  */
      if (min == NULL_TREE)
	continue;

      /* Find the edge to register the assert expr on.  */
      e = find_edge (bb, label_to_block (CASE_LABEL (cl)));

      /* Register the necessary assertions for the operand in the
	 SWITCH_EXPR.  */
      need_assert |= register_edge_assert_for (op, e, bsi,
					       max ? GE_EXPR : EQ_EXPR,
					       op,
					       fold_convert (TREE_TYPE (op),
							     min));
      if (max)
	{
	  need_assert |= register_edge_assert_for (op, e, bsi, LE_EXPR,
						   op,
						   fold_convert (TREE_TYPE (op),
								 max));
	}
    }

  return need_assert;
}


/* Traverse all the statements in block BB looking for statements that
   may generate useful assertions for the SSA names in their operand.
   If a statement produces a useful assertion A for name N_i, then the
   list of assertions already generated for N_i is scanned to
   determine if A is actually needed.

   If N_i already had the assertion A at a location dominating the
   current location, then nothing needs to be done.  Otherwise, the
   new location for A is recorded instead.

   1- For every statement S in BB, all the variables used by S are
      added to bitmap FOUND_IN_SUBGRAPH.

   2- If statement S uses an operand N in a way that exposes a known
      value range for N, then if N was not already generated by an
      ASSERT_EXPR, create a new assert location for N.  For instance,
      if N is a pointer and the statement dereferences it, we can
      assume that N is not NULL.

   3- COND_EXPRs are a special case of #2.  We can derive range
      information from the predicate but need to insert different
      ASSERT_EXPRs for each of the sub-graphs rooted at the
      conditional block.  If the last statement of BB is a conditional
      expression of the form 'X op Y', then

      a) Remove X and Y from the set FOUND_IN_SUBGRAPH.

      b) If the conditional is the only entry point to the sub-graph
	 corresponding to the THEN_CLAUSE, recurse into it.  On
	 return, if X and/or Y are marked in FOUND_IN_SUBGRAPH, then
	 an ASSERT_EXPR is added for the corresponding variable.

      c) Repeat step (b) on the ELSE_CLAUSE.

      d) Mark X and Y in FOUND_IN_SUBGRAPH.

      For instance,

	    if (a == 9)
	      b = a;
	    else
	      b = c + 1;

      In this case, an assertion on the THEN clause is useful to
      determine that 'a' is always 9 on that edge.  However, an assertion
      on the ELSE clause would be unnecessary.

   4- If BB does not end in a conditional expression, then we recurse
      into BB's dominator children.

   At the end of the recursive traversal, every SSA name will have a
   list of locations where ASSERT_EXPRs should be added.  When a new
   location for name N is found, it is registered by calling
   register_new_assert_for.  That function keeps track of all the
   registered assertions to prevent adding unnecessary assertions.
   For instance, if a pointer P_4 is dereferenced more than once in a
   dominator tree, only the location dominating all the dereference of
   P_4 will receive an ASSERT_EXPR.

   If this function returns true, then it means that there are names
   for which we need to generate ASSERT_EXPRs.  Those assertions are
   inserted by process_assert_insertions.  */

static bool
find_assert_locations_1 (basic_block bb, sbitmap live)
{
  gimple_stmt_iterator si;
  GIMPLE_type last;
  GIMPLE_type phi;
  bool need_assert;

  need_assert = false;
  last = last_stmt (bb);

  /* If BB's last statement is a conditional statement involving integer
     operands, determine if we need to add ASSERT_EXPRs.  */
  if (last
      && gimple_code (last) == GIMPLE_COND
      && !fp_predicate (last)
      && !ZERO_SSA_OPERANDS (last, SSA_OP_USE))
    need_assert |= find_conditional_asserts (bb, last);

  /* If BB's last statement is a switch statement involving integer
     operands, determine if we need to add ASSERT_EXPRs.  */
  if (last
      && gimple_code (last) == GIMPLE_SWITCH
      && !ZERO_SSA_OPERANDS (last, SSA_OP_USE))
    need_assert |= find_switch_asserts (bb, last);

  /* Traverse all the statements in BB marking used names and looking
     for statements that may infer assertions for their used operands.  */
  for (si = gsi_start_bb (bb); !gsi_end_p (si); gsi_next (&si))
    {
      GIMPLE_type stmt;
      tree op;
      ssa_op_iter i;

      stmt = gsi_stmt (si);

      if (is_gimple_debug (stmt))
	continue;

      /* See if we can derive an assertion for any of STMT's operands.  */
      FOR_EACH_SSA_TREE_OPERAND (op, stmt, i, SSA_OP_USE)
	{
	  tree value;
	  enum tree_code comp_code;

	  /* Mark OP in our live bitmap.  */
	  SET_BIT (live, SSA_NAME_VERSION (op));

	  /* If OP is used in such a way that we can infer a value
	     range for it, and we don't find a previous assertion for
	     it, create a new assertion location node for OP.  */
	  if (infer_value_range (stmt, op, &comp_code, &value))
	    {
	      /* If we are able to infer a nonzero value range for OP,
		 then walk backwards through the use-def chain to see if OP
		 was set via a typecast.

		 If so, then we can also infer a nonzero value range
		 for the operand of the NOP_EXPR.  */
	      if (comp_code == NE_EXPR && integer_zerop (value))
		{
		  tree t = op;
        GIMPLE_type def_stmt = SSA_NAME_DEF_STMT (t);

		  while (is_gimple_assign (def_stmt)
			 && gimple_assign_rhs_code (def_stmt)  == NOP_EXPR
			 && TREE_CODE
			     (gimple_assign_rhs1 (def_stmt)) == SSA_NAME
			 && POINTER_TYPE_P
			     (TREE_TYPE (gimple_assign_rhs1 (def_stmt))))
		    {
		      t = gimple_assign_rhs1 (def_stmt);
		      def_stmt = SSA_NAME_DEF_STMT (t);

		      /* Note we want to register the assert for the
			 operand of the NOP_EXPR after SI, not after the
			 conversion.  */
		      if (! has_single_use (t))
			{
			  register_new_assert_for (t, t, comp_code, value,
						   bb, NULL, si);
			  need_assert = true;
			}
		    }
		}

	      /* If OP is used only once, namely in this STMT, don't
		 bother creating an ASSERT_EXPR for it.  Such an
		 ASSERT_EXPR would do nothing but increase compile time.  */
	      if (!has_single_use (op))
		{
		  register_new_assert_for (op, op, comp_code, value,
					   bb, NULL, si);
		  need_assert = true;
		}
	    }
	}
    }

  /* Traverse all PHI nodes in BB marking used operands.  */
  for (si = gsi_start_phis (bb); !gsi_end_p(si); gsi_next (&si))
    {
      use_operand_p arg_p;
      ssa_op_iter i;
      phi = gsi_stmt (si);

      FOR_EACH_PHI_ARG (arg_p, phi, i, SSA_OP_USE)
	{
	  tree arg = USE_FROM_PTR (arg_p);
	  if (TREE_CODE (arg) == SSA_NAME)
	    SET_BIT (live, SSA_NAME_VERSION (arg));
	}
    }

  return need_assert;
}

/* Do an RPO walk over the function computing SSA name liveness
   on-the-fly and deciding on assert expressions to insert.
   Returns true if there are assert expressions to be inserted.  */

static bool
find_assert_locations (void)
{
  int *rpo = XCNEWVEC (int, last_basic_block + NUM_FIXED_BLOCKS);
  int *bb_rpo = XCNEWVEC (int, last_basic_block + NUM_FIXED_BLOCKS);
  int *last_rpo = XCNEWVEC (int, last_basic_block + NUM_FIXED_BLOCKS);
  int rpo_cnt, i;
  bool need_asserts;

  live = XCNEWVEC (sbitmap, last_basic_block + NUM_FIXED_BLOCKS);
  rpo_cnt = pre_and_rev_post_order_compute (NULL, rpo, false);
  for (i = 0; i < rpo_cnt; ++i)
    bb_rpo[rpo[i]] = i;

  need_asserts = false;
  for (i = rpo_cnt-1; i >= 0; --i)
    {
      basic_block bb = BASIC_BLOCK (rpo[i]);
      edge e;
      edge_iterator ei;

      if (!live[rpo[i]])
	{
	  live[rpo[i]] = sbitmap_alloc (num_ssa_names);
	  sbitmap_zero (live[rpo[i]]);
	}

      /* Process BB and update the live information with uses in
         this block.  */
      need_asserts |= find_assert_locations_1 (bb, live[rpo[i]]);

      /* Merge liveness into the predecessor blocks and free it.  */
      if (!sbitmap_empty_p (live[rpo[i]]))
	{
	  int pred_rpo = i;
	  FOR_EACH_EDGE (e, ei, bb->preds)
	    {
	      int pred = e->src->index;
	      if (e->flags & EDGE_DFS_BACK)
		continue;

	      if (!live[pred])
		{
		  live[pred] = sbitmap_alloc (num_ssa_names);
		  sbitmap_zero (live[pred]);
		}
	      sbitmap_a_or_b (live[pred], live[pred], live[rpo[i]]);

	      if (bb_rpo[pred] < pred_rpo)
		pred_rpo = bb_rpo[pred];
	    }

	  /* Record the RPO number of the last visited block that needs
	     live information from this block.  */
	  last_rpo[rpo[i]] = pred_rpo;
	}
      else
	{
	  sbitmap_free (live[rpo[i]]);
	  live[rpo[i]] = NULL;
	}

      /* We can free all successors live bitmaps if all their
         predecessors have been visited already.  */
      FOR_EACH_EDGE (e, ei, bb->succs)
	if (last_rpo[e->dest->index] == i
	    && live[e->dest->index])
	  {
	    sbitmap_free (live[e->dest->index]);
	    live[e->dest->index] = NULL;
	  }
    }

  XDELETEVEC (rpo);
  XDELETEVEC (bb_rpo);
  XDELETEVEC (last_rpo);
  for (i = 0; i < last_basic_block + NUM_FIXED_BLOCKS; ++i)
    if (live[i])
      sbitmap_free (live[i]);
  XDELETEVEC (live);

  return need_asserts;
}

/* Create an ASSERT_EXPR for NAME and insert it in the location
   indicated by LOC.  Return true if we made any edge insertions.  */

static bool
process_assert_insertions_for (tree name, assert_locus_t loc)
{
  /* Build the comparison expression NAME_i COMP_CODE VAL.  */
  GIMPLE_type stmt;
  tree cond;
  GIMPLE_type assert_stmt;
  edge_iterator ei;
  edge e;

  /* If we have X <=> X do not insert an assert expr for that.  */
  if (loc->expr == loc->val)
    return false;

  cond = build2 (loc->comp_code, boolean_type_node, loc->expr, loc->val);
  assert_stmt = build_assert_expr_for (cond, name);
  if (loc->e)
    {
      /* We have been asked to insert the assertion on an edge.  This
	 is used only by COND_EXPR and SWITCH_EXPR assertions.  */
#if defined ENABLE_CHECKING
      gcc_assert (gimple_code (gsi_stmt (loc->si)) == GIMPLE_COND
	  || gimple_code (gsi_stmt (loc->si)) == GIMPLE_SWITCH);
#endif

      gsi_insert_on_edge (loc->e, assert_stmt);
      return true;
    }

  /* Otherwise, we can insert right after LOC->SI iff the
     statement must not be the last statement in the block.  */
  stmt = gsi_stmt (loc->si);
  if (!stmt_ends_bb_p (stmt))
    {
      gsi_insert_after (&loc->si, assert_stmt, GSI_SAME_STMT);
      return false;
    }

  /* If STMT must be the last statement in BB, we can only insert new
     assertions on the non-abnormal edge out of BB.  Note that since
     STMT is not control flow, there may only be one non-abnormal edge
     out of BB.  */
  FOR_EACH_EDGE (e, ei, loc->bb->succs)
    if (!(e->flags & EDGE_ABNORMAL))
      {
	gsi_insert_on_edge (e, assert_stmt);
	return true;
      }

  gcc_unreachable ();
}


/* Process all the insertions registered for every name N_i registered
   in NEED_ASSERT_FOR.  The list of assertions to be inserted are
   found in ASSERTS_FOR[i].  */

static void
process_assert_insertions (void)
{
  unsigned i;
  bitmap_iterator bi;
  bool update_edges_p = false;
  int num_asserts = 0;

  if (dump_file && (dump_flags & TDF_DETAILS))
    dump_all_asserts (dump_file);

  EXECUTE_IF_SET_IN_BITMAP (need_assert_for, 0, i, bi)
    {
      assert_locus_t loc = asserts_for[i];
      gcc_assert (loc);

      while (loc)
	{
	  assert_locus_t next = loc->next;
	  update_edges_p |= process_assert_insertions_for (ssa_name (i), loc);
	  free (loc);
	  loc = next;
	  num_asserts++;
	}
    }

  if (update_edges_p)
    gsi_commit_edge_inserts ();

  statistics_counter_event (cfun, "Number of ASSERT_EXPR expressions inserted",
			    num_asserts);
}


/* Traverse the flowgraph looking for conditional jumps to insert range
   expressions.  These range expressions are meant to provide information
   to optimizations that need to reason in terms of value ranges.  They
   will not be expanded into RTL.  For instance, given:

   x = ...
   y = ...
   if (x < y)
     y = x - 2;
   else
     x = y + 3;

   this pass will transform the code into:

   x = ...
   y = ...
   if (x < y)
    {
      x = ASSERT_EXPR <x, x < y>
      y = x - 2
    }
   else
    {
      y = ASSERT_EXPR <y, x <= y>
      x = y + 3
    }

   The idea is that once copy and constant propagation have run, other
   optimizations will be able to determine what ranges of values can 'x'
   take in different paths of the code, simply by checking the reaching
   definition of 'x'.  */

static void
insert_range_assertions (void)
{
  need_assert_for = BITMAP_ALLOC (NULL);
  asserts_for = XCNEWVEC (assert_locus_t, num_ssa_names);

  calculate_dominance_info (CDI_DOMINATORS);

  if (find_assert_locations ())
    {
      process_assert_insertions ();
      update_ssa (TODO_update_ssa_no_phi);
    }

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "\nSSA form after inserting ASSERT_EXPRs\n");
      dump_function_to_file (current_function_decl, dump_file, dump_flags);
    }

  free (asserts_for);
  BITMAP_FREE (need_assert_for);
}

/* Checks one ARRAY_REF in REF, located at LOCUS. Ignores flexible arrays
   and "struct" hacks. If VRP can determine that the
   array subscript is a constant, check if it is outside valid
   range. If the array subscript is a RANGE, warn if it is
   non-overlapping with valid range.
   IGNORE_OFF_BY_ONE is true if the ARRAY_REF is inside a ADDR_EXPR.  */

static void
check_array_ref (location_t location, tree ref, bool ignore_off_by_one)
{
  value_range_t* vr = NULL;
  tree low_sub, up_sub;
  tree low_bound, up_bound = array_ref_up_bound (ref);

  low_sub = up_sub = TREE_OPERAND (ref, 1);

  if (!up_bound || TREE_NO_WARNING (ref)
      || TREE_CODE (up_bound) != INTEGER_CST
      /* Can not check flexible arrays.  */
      || (TYPE_SIZE (TREE_TYPE (ref)) == NULL_TREE
          && TYPE_DOMAIN (TREE_TYPE (ref)) != NULL_TREE
          && TYPE_MAX_VALUE (TYPE_DOMAIN (TREE_TYPE (ref))) == NULL_TREE)
      /* Accesses after the end of arrays of size 0 (gcc
         extension) and 1 are likely intentional ("struct
         hack").  */
      || compare_tree_int (up_bound, 1) <= 0)
    return;

  low_bound = array_ref_low_bound (ref);

  if (TREE_CODE (low_sub) == SSA_NAME)
    {
      vr = get_value_range (low_sub);
      if (vr->type == VR_RANGE || vr->type == VR_ANTI_RANGE)
        {
          low_sub = vr->type == VR_RANGE ? vr->max : vr->min;
          up_sub = vr->type == VR_RANGE ? vr->min : vr->max;
        }
    }

  if (vr && vr->type == VR_ANTI_RANGE)
    {
      if (TREE_CODE (up_sub) == INTEGER_CST
          && tree_int_cst_lt (up_bound, up_sub)
          && TREE_CODE (low_sub) == INTEGER_CST
          && tree_int_cst_lt (low_sub, low_bound))
        {
          warning_at (location, OPT_Warray_bounds,
		      "array subscript is outside array bounds");
          TREE_NO_WARNING (ref) = 1;
        }
    }
  else if (TREE_CODE (up_sub) == INTEGER_CST
           && tree_int_cst_lt (up_bound, up_sub)
           && !tree_int_cst_equal (up_bound, up_sub)
           && (!ignore_off_by_one
               || !tree_int_cst_equal (int_const_binop (PLUS_EXPR,
                                                        up_bound,
                                                        integer_one_node,
                                                        0),
                                       up_sub)))
    {
      warning_at (location, OPT_Warray_bounds,
		  "array subscript is above array bounds");
      TREE_NO_WARNING (ref) = 1;
    }
  else if (TREE_CODE (low_sub) == INTEGER_CST
           && tree_int_cst_lt (low_sub, low_bound))
    {
      warning_at (location, OPT_Warray_bounds,
		  "array subscript is below array bounds");
      TREE_NO_WARNING (ref) = 1;
    }
}

/* Searches if the expr T, located at LOCATION computes
   address of an ARRAY_REF, and call check_array_ref on it.  */

static void
search_for_addr_array (tree t, location_t location)
{
  while (TREE_CODE (t) == SSA_NAME)
    {
      GIMPLE_type g = SSA_NAME_DEF_STMT (t);

      if (gimple_code (g) != GIMPLE_ASSIGN)
	return;

      if (get_gimple_rhs_class (gimple_assign_rhs_code (g))
	  != GIMPLE_SINGLE_RHS)
	return;

      t = gimple_assign_rhs1 (g);
    }


  /* We are only interested in addresses of ARRAY_REF's.  */
  if (TREE_CODE (t) != ADDR_EXPR)
    return;

  /* Check each ARRAY_REFs in the reference chain. */
  do
    {
      if (TREE_CODE (t) == ARRAY_REF)
	check_array_ref (location, t, true /*ignore_off_by_one*/);

      t = TREE_OPERAND (t, 0);
    }
  while (handled_component_p (t));
}

/* walk_tree() callback that checks if *TP is
   an ARRAY_REF inside an ADDR_EXPR (in which an array
   subscript one outside the valid range is allowed). Call
   check_array_ref for each ARRAY_REF found. The location is
   passed in DATA.  */

static tree
check_array_bounds (tree *tp, int *walk_subtree, void *data)
{
  tree t = *tp;
  struct walk_stmt_info *wi = (struct walk_stmt_info *) data;
  location_t location;

  if (EXPR_HAS_LOCATION (t))
    location = EXPR_LOCATION (t);
  else
    {
      location_t *locp = (location_t *) wi->info;
      location = *locp;
    }

  *walk_subtree = TRUE;

  if (TREE_CODE (t) == ARRAY_REF)
    check_array_ref (location, t, false /*ignore_off_by_one*/);

  if (TREE_CODE (t) == INDIRECT_REF
      || (TREE_CODE (t) == RETURN_EXPR && TREE_OPERAND (t, 0)))
    search_for_addr_array (TREE_OPERAND (t, 0), location);

  if (TREE_CODE (t) == ADDR_EXPR)
    *walk_subtree = FALSE;

  return NULL_TREE;
}

/* Walk over all statements of all reachable BBs and call check_array_bounds
   on them.  */

static void
check_all_array_refs (void)
{
  basic_block bb;
  gimple_stmt_iterator si;

  FOR_EACH_BB (bb)
    {
      edge_iterator ei;
      edge e;
      bool executable = false;

      /* Skip blocks that were found to be unreachable.  */
      FOR_EACH_EDGE (e, ei, bb->preds)
	executable |= !!(e->flags & EDGE_EXECUTABLE);
      if (!executable)
	continue;

      for (si = gsi_start_bb (bb); !gsi_end_p (si); gsi_next (&si))
	{
     GIMPLE_type stmt = gsi_stmt (si);
	  struct walk_stmt_info wi;
	  if (!gimple_has_location (stmt))
	    continue;

	  if (is_gimple_call (stmt))
	    {
	      size_t i;
	      size_t n = gimple_call_num_args (stmt);
	      for (i = 0; i < n; i++)
		{
		  tree arg = gimple_call_arg (stmt, i);
		  search_for_addr_array (arg, gimple_location (stmt));
		}
	    }
	  else
	    {
	      memset (&wi, 0, sizeof (wi));
	      wi.info = CONST_CAST (void *, (const void *)
				    gimple_location_ptr (stmt));

	      walk_gimple_op (gsi_stmt (si),
			      check_array_bounds,
			      &wi);
	    }
	}
    }
}

/* Convert range assertion expressions into the implied copies and
   copy propagate away the copies.  Doing the trivial copy propagation
   here avoids the need to run the full copy propagation pass after
   VRP.

   FIXME, this will eventually lead to copy propagation removing the
   names that had useful range information attached to them.  For
   instance, if we had the assertion N_i = ASSERT_EXPR <N_j, N_j > 3>,
   then N_i will have the range [3, +INF].

   However, by converting the assertion into the implied copy
   operation N_i = N_j, we will then copy-propagate N_j into the uses
   of N_i and lose the range information.  We may want to hold on to
   ASSERT_EXPRs a little while longer as the ranges could be used in
   things like jump threading.

   The problem with keeping ASSERT_EXPRs around is that passes after
   VRP need to handle them appropriately.

   Another approach would be to make the range information a first
   class property of the SSA_NAME so that it can be queried from
   any pass.  This is made somewhat more complex by the need for
   multiple ranges to be associated with one SSA_NAME.  */

static void
remove_range_assertions (void)
{
  basic_block bb;
  gimple_stmt_iterator si;

  /* Note that the BSI iterator bump happens at the bottom of the
     loop and no bump is necessary if we're removing the statement
     referenced by the current BSI.  */
  FOR_EACH_BB (bb)
    for (si = gsi_start_bb (bb); !gsi_end_p (si);)
      {
   GIMPLE_type stmt = gsi_stmt (si);
   GIMPLE_type use_stmt;

	if (is_gimple_assign (stmt)
	    && gimple_assign_rhs_code (stmt) == ASSERT_EXPR)
	  {
	    tree rhs = gimple_assign_rhs1 (stmt);
	    tree var;
	    tree cond = fold (ASSERT_EXPR_COND (rhs));
	    use_operand_p use_p;
	    imm_use_iterator iter;

	    gcc_assert (cond != boolean_false_node);

	    /* Propagate the RHS into every use of the LHS.  */
	    var = ASSERT_EXPR_VAR (rhs);
	    FOR_EACH_IMM_USE_STMT (use_stmt, iter,
				   gimple_assign_lhs (stmt))
	      FOR_EACH_IMM_USE_ON_STMT (use_p, iter)
		{
		  SET_USE (use_p, var);
		  gcc_assert (TREE_CODE (var) == SSA_NAME);
		}

	    /* And finally, remove the copy, it is not needed.  */
	    gsi_remove (&si, true);
	    release_defs (stmt);
	  }
	else
	  gsi_next (&si);
      }
}


/* Return true if STMT is interesting for VRP.  */

static bool
stmt_interesting_for_vrp (GIMPLE_type stmt)
{
  if (gimple_code (stmt) == GIMPLE_PHI
      && is_gimple_reg (gimple_phi_result (stmt))
      && (INTEGRAL_TYPE_P (TREE_TYPE (gimple_phi_result (stmt)))
	  || POINTER_TYPE_P (TREE_TYPE (gimple_phi_result (stmt)))))
    return true;
  else if (is_gimple_assign (stmt) || is_gimple_call (stmt))
    {
      tree lhs = gimple_get_lhs (stmt);

      /* In general, assignments with virtual operands are not useful
	 for deriving ranges, with the obvious exception of calls to
	 builtin functions.  */
      if (lhs && TREE_CODE (lhs) == SSA_NAME
	  && (INTEGRAL_TYPE_P (TREE_TYPE (lhs))
	      || POINTER_TYPE_P (TREE_TYPE (lhs)))
	  && ((is_gimple_call (stmt)
	       && gimple_call_fndecl (stmt) != NULL_TREE
	       && DECL_IS_BUILTIN (gimple_call_fndecl (stmt)))
	      || !gimple_vuse (stmt)))
	return true;
    }
  else if (gimple_code (stmt) == GIMPLE_COND
	   || gimple_code (stmt) == GIMPLE_SWITCH)
    return true;

  return false;
}


/* Initialize local data structures for VRP.  */

static void
vrp_initialize (void)
{
  basic_block bb;

  vr_value = XCNEWVEC (value_range_t *, num_ssa_names);
  vr_phi_edge_counts = XCNEWVEC (int, num_ssa_names);

  FOR_EACH_BB (bb)
    {
      gimple_stmt_iterator si;

      for (si = gsi_start_phis (bb); !gsi_end_p (si); gsi_next (&si))
	{
     GIMPLE_type phi = gsi_stmt (si);
	  if (!stmt_interesting_for_vrp (phi))
	    {
	      tree lhs = PHI_RESULT (phi);
	      set_value_range_to_varying (get_value_range (lhs));
	      prop_set_simulate_again (phi, false);
	    }
	  else
	    prop_set_simulate_again (phi, true);
	}

      for (si = gsi_start_bb (bb); !gsi_end_p (si); gsi_next (&si))
        {
     GIMPLE_type stmt = gsi_stmt (si);

 	  /* If the statement is a control insn, then we do not
 	     want to avoid simulating the statement once.  Failure
 	     to do so means that those edges will never get added.  */
	  if (stmt_ends_bb_p (stmt))
	    prop_set_simulate_again (stmt, true);
	  else if (!stmt_interesting_for_vrp (stmt))
	    {
	      ssa_op_iter i;
	      tree def;
	      FOR_EACH_SSA_TREE_OPERAND (def, stmt, i, SSA_OP_DEF)
		set_value_range_to_varying (get_value_range (def));
	      prop_set_simulate_again (stmt, false);
	    }
	  else
	    prop_set_simulate_again (stmt, true);
	}
    }
}


/* Visit assignment STMT.  If it produces an interesting range, record
   the SSA name in *OUTPUT_P.  */

static enum ssa_prop_result
vrp_visit_assignment_or_call (GIMPLE_type stmt, tree *output_p)
{
  tree def, lhs;
  ssa_op_iter iter;
  enum gimple_code code = gimple_code (stmt);
  lhs = gimple_get_lhs (stmt);

  /* We only keep track of ranges in integral and pointer types.  */
  if (TREE_CODE (lhs) == SSA_NAME
      && ((INTEGRAL_TYPE_P (TREE_TYPE (lhs))
	   /* It is valid to have NULL MIN/MAX values on a type.  See
	      build_range_type.  */
	   && TYPE_MIN_VALUE (TREE_TYPE (lhs))
	   && TYPE_MAX_VALUE (TREE_TYPE (lhs)))
	  || POINTER_TYPE_P (TREE_TYPE (lhs))))
    {
      value_range_t new_vr = { VR_UNDEFINED, NULL_TREE, NULL_TREE, NULL };

      if (code == GIMPLE_CALL)
	extract_range_basic (&new_vr, stmt);
      else
	extract_range_from_assignment (&new_vr, stmt);

      if (update_value_range (lhs, &new_vr))
	{
	  *output_p = lhs;

	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      fprintf (dump_file, "Found new range for ");
	      print_generic_expr (dump_file, lhs, 0);
	      fprintf (dump_file, ": ");
	      dump_value_range (dump_file, &new_vr);
	      fprintf (dump_file, "\n\n");
	    }

	  if (new_vr.type == VR_VARYING)
	    return SSA_PROP_VARYING;

	  return SSA_PROP_INTERESTING;
	}

      return SSA_PROP_NOT_INTERESTING;
    }

  /* Every other statement produces no useful ranges.  */
  FOR_EACH_SSA_TREE_OPERAND (def, stmt, iter, SSA_OP_DEF)
    set_value_range_to_varying (get_value_range (def));

  return SSA_PROP_VARYING;
}

/* Helper that gets the value range of the SSA_NAME with version I
   or a symbolic range containing the SSA_NAME only if the value range
   is varying or undefined.  */

static inline value_range_t
get_vr_for_comparison (int i)
{
  value_range_t vr = *(vr_value[i]);

  /* If name N_i does not have a valid range, use N_i as its own
     range.  This allows us to compare against names that may
     have N_i in their ranges.  */
  if (vr.type == VR_VARYING || vr.type == VR_UNDEFINED)
    {
      vr.type = VR_RANGE;
      vr.min = ssa_name (i);
      vr.max = ssa_name (i);
    }

  return vr;
}

/* Compare all the value ranges for names equivalent to VAR with VAL
   using comparison code COMP.  Return the same value returned by
   compare_range_with_value, including the setting of
   *STRICT_OVERFLOW_P.  */

static tree
compare_name_with_value (enum tree_code comp, tree var, tree val,
			 bool *strict_overflow_p)
{
  bitmap_iterator bi;
  unsigned i;
  bitmap e;
  tree retval, t;
  int used_strict_overflow;
  bool sop;
  value_range_t equiv_vr;

  /* Get the set of equivalences for VAR.  */
  e = get_value_range (var)->equiv;

  /* Start at -1.  Set it to 0 if we do a comparison without relying
     on overflow, or 1 if all comparisons rely on overflow.  */
  used_strict_overflow = -1;

  /* Compare vars' value range with val.  */
  equiv_vr = get_vr_for_comparison (SSA_NAME_VERSION (var));
  sop = false;
  retval = compare_range_with_value (comp, &equiv_vr, val, &sop);
  if (retval)
    used_strict_overflow = sop ? 1 : 0;

  /* If the equiv set is empty we have done all work we need to do.  */
  if (e == NULL)
    {
      if (retval
	  && used_strict_overflow > 0)
	*strict_overflow_p = true;
      return retval;
    }

  EXECUTE_IF_SET_IN_BITMAP (e, 0, i, bi)
    {
      equiv_vr = get_vr_for_comparison (i);
      sop = false;
      t = compare_range_with_value (comp, &equiv_vr, val, &sop);
      if (t)
	{
	  /* If we get different answers from different members
	     of the equivalence set this check must be in a dead
	     code region.  Folding it to a trap representation
	     would be correct here.  For now just return don't-know.  */
	  if (retval != NULL
	      && t != retval)
	    {
	      retval = NULL_TREE;
	      break;
	    }
	  retval = t;

	  if (!sop)
	    used_strict_overflow = 0;
	  else if (used_strict_overflow < 0)
	    used_strict_overflow = 1;
	}
    }

  if (retval
      && used_strict_overflow > 0)
    *strict_overflow_p = true;

  return retval;
}


/* Given a comparison code COMP and names N1 and N2, compare all the
   ranges equivalent to N1 against all the ranges equivalent to N2
   to determine the value of N1 COMP N2.  Return the same value
   returned by compare_ranges.  Set *STRICT_OVERFLOW_P to indicate
   whether we relied on an overflow infinity in the comparison.  */


static tree
compare_names (enum tree_code comp, tree n1, tree n2,
	       bool *strict_overflow_p)
{
  tree t, retval;
  bitmap e1, e2;
  bitmap_iterator bi1, bi2;
  unsigned i1, i2;
  int used_strict_overflow;
  static bitmap_obstack *s_obstack = NULL;
  static bitmap s_e1 = NULL, s_e2 = NULL;

  /* Compare the ranges of every name equivalent to N1 against the
     ranges of every name equivalent to N2.  */
  e1 = get_value_range (n1)->equiv;
  e2 = get_value_range (n2)->equiv;

  /* Use the fake bitmaps if e1 or e2 are not available.  */
  if (s_obstack == NULL)
    {
      s_obstack = XNEW (bitmap_obstack);
      bitmap_obstack_initialize (s_obstack);
      s_e1 = BITMAP_ALLOC (s_obstack);
      s_e2 = BITMAP_ALLOC (s_obstack);
    }
  if (e1 == NULL)
    e1 = s_e1;
  if (e2 == NULL)
    e2 = s_e2;

  /* Add N1 and N2 to their own set of equivalences to avoid
     duplicating the body of the loop just to check N1 and N2
     ranges.  */
  bitmap_set_bit (e1, SSA_NAME_VERSION (n1));
  bitmap_set_bit (e2, SSA_NAME_VERSION (n2));

  /* If the equivalence sets have a common intersection, then the two
     names can be compared without checking their ranges.  */
  if (bitmap_intersect_p (e1, e2))
    {
      bitmap_clear_bit (e1, SSA_NAME_VERSION (n1));
      bitmap_clear_bit (e2, SSA_NAME_VERSION (n2));

      return (comp == EQ_EXPR || comp == GE_EXPR || comp == LE_EXPR)
	     ? boolean_true_node
	     : boolean_false_node;
    }

  /* Start at -1.  Set it to 0 if we do a comparison without relying
     on overflow, or 1 if all comparisons rely on overflow.  */
  used_strict_overflow = -1;

  /* Otherwise, compare all the equivalent ranges.  First, add N1 and
     N2 to their own set of equivalences to avoid duplicating the body
     of the loop just to check N1 and N2 ranges.  */
  EXECUTE_IF_SET_IN_BITMAP (e1, 0, i1, bi1)
    {
      value_range_t vr1 = get_vr_for_comparison (i1);

      t = retval = NULL_TREE;
      EXECUTE_IF_SET_IN_BITMAP (e2, 0, i2, bi2)
	{
	  bool sop = false;

	  value_range_t vr2 = get_vr_for_comparison (i2);

	  t = compare_ranges (comp, &vr1, &vr2, &sop);
	  if (t)
	    {
	      /* If we get different answers from different members
		 of the equivalence set this check must be in a dead
		 code region.  Folding it to a trap representation
		 would be correct here.  For now just return don't-know.  */
	      if (retval != NULL
		  && t != retval)
		{
		  bitmap_clear_bit (e1, SSA_NAME_VERSION (n1));
		  bitmap_clear_bit (e2, SSA_NAME_VERSION (n2));
		  return NULL_TREE;
		}
	      retval = t;

	      if (!sop)
		used_strict_overflow = 0;
	      else if (used_strict_overflow < 0)
		used_strict_overflow = 1;
	    }
	}

      if (retval)
	{
	  bitmap_clear_bit (e1, SSA_NAME_VERSION (n1));
	  bitmap_clear_bit (e2, SSA_NAME_VERSION (n2));
	  if (used_strict_overflow > 0)
	    *strict_overflow_p = true;
	  return retval;
	}
    }

  /* None of the equivalent ranges are useful in computing this
     comparison.  */
  bitmap_clear_bit (e1, SSA_NAME_VERSION (n1));
  bitmap_clear_bit (e2, SSA_NAME_VERSION (n2));
  return NULL_TREE;
}

/* Helper function for vrp_evaluate_conditional_warnv.  */

static tree
vrp_evaluate_conditional_warnv_with_ops_using_ranges (enum tree_code code,
						      tree op0, tree op1,
						      bool * strict_overflow_p)
{
  value_range_t *vr0, *vr1;

  vr0 = (TREE_CODE (op0) == SSA_NAME) ? get_value_range (op0) : NULL;
  vr1 = (TREE_CODE (op1) == SSA_NAME) ? get_value_range (op1) : NULL;

  if (vr0 && vr1)
    return compare_ranges (code, vr0, vr1, strict_overflow_p);
  else if (vr0 && vr1 == NULL)
    return compare_range_with_value (code, vr0, op1, strict_overflow_p);
  else if (vr0 == NULL && vr1)
    return (compare_range_with_value
	    (swap_tree_comparison (code), vr1, op0, strict_overflow_p));
  return NULL;
}

/* Helper function for vrp_evaluate_conditional_warnv. */

static tree
vrp_evaluate_conditional_warnv_with_ops (enum tree_code code, tree op0,
					 tree op1, bool use_equiv_p,
					 bool *strict_overflow_p, bool *only_ranges)
{
  tree ret;
  if (only_ranges)
    *only_ranges = true;

  /* We only deal with integral and pointer types.  */
  if (!INTEGRAL_TYPE_P (TREE_TYPE (op0))
      && !POINTER_TYPE_P (TREE_TYPE (op0)))
    return NULL_TREE;

  if (use_equiv_p)
    {
      if (only_ranges
          && (ret = vrp_evaluate_conditional_warnv_with_ops_using_ranges
	              (code, op0, op1, strict_overflow_p)))
	return ret;
      *only_ranges = false;
      if (TREE_CODE (op0) == SSA_NAME && TREE_CODE (op1) == SSA_NAME)
	return compare_names (code, op0, op1, strict_overflow_p);
      else if (TREE_CODE (op0) == SSA_NAME)
	return compare_name_with_value (code, op0, op1, strict_overflow_p);
      else if (TREE_CODE (op1) == SSA_NAME)
	return (compare_name_with_value
		(swap_tree_comparison (code), op1, op0, strict_overflow_p));
    }
  else
    return vrp_evaluate_conditional_warnv_with_ops_using_ranges (code, op0, op1,
								 strict_overflow_p);
  return NULL_TREE;
}

/* Given (CODE OP0 OP1) within STMT, try to simplify it based on value range
   information.  Return NULL if the conditional can not be evaluated.
   The ranges of all the names equivalent with the operands in COND
   will be used when trying to compute the value.  If the result is
   based on undefined signed overflow, issue a warning if
   appropriate.  */

static tree
vrp_evaluate_conditional (enum tree_code code, tree op0, tree op1, GIMPLE_type stmt)
{
  bool sop;
  tree ret;
  bool only_ranges;

  /* Some passes and foldings leak constants with overflow flag set
     into the IL.  Avoid doing wrong things with these and bail out.  */
  if ((TREE_CODE (op0) == INTEGER_CST
       && TREE_OVERFLOW (op0))
      || (TREE_CODE (op1) == INTEGER_CST
	  && TREE_OVERFLOW (op1)))
    return NULL_TREE;

  sop = false;
  ret = vrp_evaluate_conditional_warnv_with_ops (code, op0, op1, true, &sop,
  						 &only_ranges);

  if (ret && sop)
    {
      enum warn_strict_overflow_code wc;
      const char* warnmsg;

      if (is_gimple_min_invariant (ret))
	{
	  wc = WARN_STRICT_OVERFLOW_CONDITIONAL;
	  warnmsg = G_("assuming signed overflow does not occur when "
		       "simplifying conditional to constant");
	}
      else
	{
	  wc = WARN_STRICT_OVERFLOW_COMPARISON;
	  warnmsg = G_("assuming signed overflow does not occur when "
		       "simplifying conditional");
	}

      if (issue_strict_overflow_warning (wc))
	{
	  location_t location;

	  if (!gimple_has_location (stmt))
	    location = input_location;
	  else
	    location = gimple_location (stmt);
	  warning_at (location, OPT_Wstrict_overflow, "%s", warnmsg);
	}
    }

  if (warn_type_limits
      && ret && only_ranges
      && TREE_CODE_CLASS (code) == tcc_comparison
      && TREE_CODE (op0) == SSA_NAME)
    {
      /* If the comparison is being folded and the operand on the LHS
	 is being compared against a constant value that is outside of
	 the natural range of OP0's type, then the predicate will
	 always fold regardless of the value of OP0.  If -Wtype-limits
	 was specified, emit a warning.  */
      tree type = TREE_TYPE (op0);
      value_range_t *vr0 = get_value_range (op0);

      if (vr0->type != VR_VARYING
	  && INTEGRAL_TYPE_P (type)
	  && vrp_val_is_min (vr0->min)
	  && vrp_val_is_max (vr0->max)
	  && is_gimple_min_invariant (op1))
	{
	  location_t location;

	  if (!gimple_has_location (stmt))
	    location = input_location;
	  else
	    location = gimple_location (stmt);

	  warning_at (location, OPT_Wtype_limits,
		      integer_zerop (ret)
		      ? G_("comparison always false "
                           "due to limited range of data type")
		      : G_("comparison always true "
                           "due to limited range of data type"));
	}
    }

  return ret;
}


/* Visit conditional statement STMT.  If we can determine which edge
   will be taken out of STMT's basic block, record it in
   *TAKEN_EDGE_P and return SSA_PROP_INTERESTING.  Otherwise, return
   SSA_PROP_VARYING.  */

static enum ssa_prop_result
vrp_visit_cond_stmt (GIMPLE_type stmt, edge *taken_edge_p)
{
  tree val;
  bool sop;

  *taken_edge_p = NULL;

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      tree use;
      ssa_op_iter i;

      fprintf (dump_file, "\nVisiting conditional with predicate: ");
      print_gimple_stmt (dump_file, stmt, 0, 0);
      fprintf (dump_file, "\nWith known ranges\n");

      FOR_EACH_SSA_TREE_OPERAND (use, stmt, i, SSA_OP_USE)
	{
	  fprintf (dump_file, "\t");
	  print_generic_expr (dump_file, use, 0);
	  fprintf (dump_file, ": ");
	  dump_value_range (dump_file, vr_value[SSA_NAME_VERSION (use)]);
	}

      fprintf (dump_file, "\n");
    }

  /* Compute the value of the predicate COND by checking the known
     ranges of each of its operands.

     Note that we cannot evaluate all the equivalent ranges here
     because those ranges may not yet be final and with the current
     propagation strategy, we cannot determine when the value ranges
     of the names in the equivalence set have changed.

     For instance, given the following code fragment

        i_5 = PHI <8, i_13>
	...
     	i_14 = ASSERT_EXPR <i_5, i_5 != 0>
	if (i_14 == 1)
	  ...

     Assume that on the first visit to i_14, i_5 has the temporary
     range [8, 8] because the second argument to the PHI function is
     not yet executable.  We derive the range ~[0, 0] for i_14 and the
     equivalence set { i_5 }.  So, when we visit 'if (i_14 == 1)' for
     the first time, since i_14 is equivalent to the range [8, 8], we
     determine that the predicate is always false.

     On the next round of propagation, i_13 is determined to be
     VARYING, which causes i_5 to drop down to VARYING.  So, another
     visit to i_14 is scheduled.  In this second visit, we compute the
     exact same range and equivalence set for i_14, namely ~[0, 0] and
     { i_5 }.  But we did not have the previous range for i_5
     registered, so vrp_visit_assignment thinks that the range for
     i_14 has not changed.  Therefore, the predicate 'if (i_14 == 1)'
     is not visited again, which stops propagation from visiting
     statements in the THEN clause of that if().

     To properly fix this we would need to keep the previous range
     value for the names in the equivalence set.  This way we would've
     discovered that from one visit to the other i_5 changed from
     range [8, 8] to VR_VARYING.

     However, fixing this apparent limitation may not be worth the
     additional checking.  Testing on several code bases (GCC, DLV,
     MICO, TRAMP3D and SPEC2000) showed that doing this results in
     4 more predicates folded in SPEC.  */
  sop = false;

  val = vrp_evaluate_conditional_warnv_with_ops (gimple_cond_code (stmt),
						 gimple_cond_lhs (stmt),
						 gimple_cond_rhs (stmt),
						 false, &sop, NULL);
  if (val)
    {
      if (!sop)
	*taken_edge_p = find_taken_edge (gimple_bb (stmt), val);
      else
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file,
		     "\nIgnoring predicate evaluation because "
		     "it assumes that signed overflow is undefined");
	  val = NULL_TREE;
	}
    }

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "\nPredicate evaluates to: ");
      if (val == NULL_TREE)
	fprintf (dump_file, "DON'T KNOW\n");
      else
	print_generic_stmt (dump_file, val, 0);
    }

  return (*taken_edge_p) ? SSA_PROP_INTERESTING : SSA_PROP_VARYING;
}

/* Searches the case label vector VEC for the index *IDX of the CASE_LABEL
   that includes the value VAL.  The search is restricted to the range
   [START_IDX, n - 1] where n is the size of VEC.

   If there is a CASE_LABEL for VAL, its index is placed in IDX and true is
   returned.

   If there is no CASE_LABEL for VAL and there is one that is larger than VAL,
   it is placed in IDX and false is returned.

   If VAL is larger than any CASE_LABEL, n is placed on IDX and false is
   returned. */

static bool
find_case_label_index (GIMPLE_type stmt, size_t start_idx, tree val, size_t *idx)
{
  size_t n = gimple_switch_num_labels (stmt);
  size_t low, high;

  /* Find case label for minimum of the value range or the next one.
     At each iteration we are searching in [low, high - 1]. */

  for (low = start_idx, high = n; high != low; )
    {
      tree t;
      int cmp;
      /* Note that i != high, so we never ask for n. */
      size_t i = (high + low) / 2;
      t = gimple_switch_label (stmt, i);

      /* Cache the result of comparing CASE_LOW and val.  */
      cmp = tree_int_cst_compare (CASE_LOW (t), val);

      if (cmp == 0)
	{
	  /* Ranges cannot be empty. */
	  *idx = i;
	  return true;
	}
      else if (cmp > 0)
        high = i;
      else
	{
	  low = i + 1;
	  if (CASE_HIGH (t) != NULL
	      && tree_int_cst_compare (CASE_HIGH (t), val) >= 0)
	    {
	      *idx = i;
	      return true;
	    }
        }
    }

  *idx = high;
  return false;
}

/* Searches the case label vector VEC for the range of CASE_LABELs that is used
   for values between MIN and MAX. The first index is placed in MIN_IDX. The
   last index is placed in MAX_IDX. If the range of CASE_LABELs is empty
   then MAX_IDX < MIN_IDX.
   Returns true if the default label is not needed. */

static bool
find_case_label_range (GIMPLE_type stmt, tree min, tree max, size_t *min_idx,
		       size_t *max_idx)
{
  size_t i, j;
  bool min_take_default = !find_case_label_index (stmt, 1, min, &i);
  bool max_take_default = !find_case_label_index (stmt, i, max, &j);

  if (i == j
      && min_take_default
      && max_take_default)
    {
      /* Only the default case label reached.
         Return an empty range. */
      *min_idx = 1;
      *max_idx = 0;
      return false;
    }
  else
    {
      bool take_default = min_take_default || max_take_default;
      tree low, high;
      size_t k;

      if (max_take_default)
	j--;

      /* If the case label range is continuous, we do not need
	 the default case label.  Verify that.  */
      high = CASE_LOW (gimple_switch_label (stmt, i));
      if (CASE_HIGH (gimple_switch_label (stmt, i)))
	high = CASE_HIGH (gimple_switch_label (stmt, i));
      for (k = i + 1; k <= j; ++k)
	{
	  low = CASE_LOW (gimple_switch_label (stmt, k));
	  if (!integer_onep (int_const_binop (MINUS_EXPR, low, high, 0)))
	    {
	      take_default = true;
	      break;
	    }
	  high = low;
	  if (CASE_HIGH (gimple_switch_label (stmt, k)))
	    high = CASE_HIGH (gimple_switch_label (stmt, k));
	}

      *min_idx = i;
      *max_idx = j;
      return !take_default;
    }
}

/* Visit switch statement STMT.  If we can determine which edge
   will be taken out of STMT's basic block, record it in
   *TAKEN_EDGE_P and return SSA_PROP_INTERESTING.  Otherwise, return
   SSA_PROP_VARYING.  */

static enum ssa_prop_result
vrp_visit_switch_stmt (GIMPLE_type stmt, edge *taken_edge_p)
{
  tree op, val;
  value_range_t *vr;
  size_t i = 0, j = 0;
  bool take_default;

  *taken_edge_p = NULL;
  op = gimple_switch_index (stmt);
  if (TREE_CODE (op) != SSA_NAME)
    return SSA_PROP_VARYING;

  vr = get_value_range (op);
  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "\nVisiting switch expression with operand ");
      print_generic_expr (dump_file, op, 0);
      fprintf (dump_file, " with known range ");
      dump_value_range (dump_file, vr);
      fprintf (dump_file, "\n");
    }

  if (vr->type != VR_RANGE
      || symbolic_range_p (vr))
    return SSA_PROP_VARYING;

  /* Find the single edge that is taken from the switch expression.  */
  take_default = !find_case_label_range (stmt, vr->min, vr->max, &i, &j);

  /* Check if the range spans no CASE_LABEL. If so, we only reach the default
     label */
  if (j < i)
    {
      gcc_assert (take_default);
      val = gimple_switch_default_label (stmt);
    }
  else
    {
      /* Check if labels with index i to j and maybe the default label
	 are all reaching the same label.  */

      val = gimple_switch_label (stmt, i);
      if (take_default
	  && CASE_LABEL (gimple_switch_default_label (stmt))
	  != CASE_LABEL (val))
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file, "  not a single destination for this "
		     "range\n");
          return SSA_PROP_VARYING;
	}
      for (++i; i <= j; ++i)
        {
          if (CASE_LABEL (gimple_switch_label (stmt, i)) != CASE_LABEL (val))
	    {
	      if (dump_file && (dump_flags & TDF_DETAILS))
		fprintf (dump_file, "  not a single destination for this "
			 "range\n");
	      return SSA_PROP_VARYING;
	    }
        }
    }

  *taken_edge_p = find_edge (gimple_bb (stmt),
			     label_to_block (CASE_LABEL (val)));

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "  will take edge to ");
      print_generic_stmt (dump_file, CASE_LABEL (val), 0);
    }

  return SSA_PROP_INTERESTING;
}


/* Evaluate statement STMT.  If the statement produces a useful range,
   return SSA_PROP_INTERESTING and record the SSA name with the
   interesting range into *OUTPUT_P.

   If STMT is a conditional branch and we can determine its truth
   value, the taken edge is recorded in *TAKEN_EDGE_P.

   If STMT produces a varying value, return SSA_PROP_VARYING.  */

static enum ssa_prop_result
vrp_visit_stmt (GIMPLE_type stmt, edge *taken_edge_p, tree *output_p)
{
  tree def;
  ssa_op_iter iter;

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "\nVisiting statement:\n");
      print_gimple_stmt (dump_file, stmt, 0, dump_flags);
      fprintf (dump_file, "\n");
    }

  if (!stmt_interesting_for_vrp (stmt))
    gcc_assert (stmt_ends_bb_p (stmt));
  else if (is_gimple_assign (stmt) || is_gimple_call (stmt))
    {
      /* In general, assignments with virtual operands are not useful
	 for deriving ranges, with the obvious exception of calls to
	 builtin functions.  */

      if ((is_gimple_call (stmt)
	   && gimple_call_fndecl (stmt) != NULL_TREE
	   && DECL_IS_BUILTIN (gimple_call_fndecl (stmt)))
	  || !gimple_vuse (stmt))
	return vrp_visit_assignment_or_call (stmt, output_p);
    }
  else if (gimple_code (stmt) == GIMPLE_COND)
    return vrp_visit_cond_stmt (stmt, taken_edge_p);
  else if (gimple_code (stmt) == GIMPLE_SWITCH)
    return vrp_visit_switch_stmt (stmt, taken_edge_p);

  /* All other statements produce nothing of interest for VRP, so mark
     their outputs varying and prevent further simulation.  */
  FOR_EACH_SSA_TREE_OPERAND (def, stmt, iter, SSA_OP_DEF)
    set_value_range_to_varying (get_value_range (def));

  return SSA_PROP_VARYING;
}


/* Meet operation for value ranges.  Given two value ranges VR0 and
   VR1, store in VR0 a range that contains both VR0 and VR1.  This
   may not be the smallest possible such range.  */

static void
vrp_meet (value_range_t *vr0, value_range_t *vr1)
{
  if (vr0->type == VR_UNDEFINED)
    {
      copy_value_range (vr0, vr1);
      return;
    }

  if (vr1->type == VR_UNDEFINED)
    {
      /* Nothing to do.  VR0 already has the resulting range.  */
      return;
    }

  if (vr0->type == VR_VARYING)
    {
      /* Nothing to do.  VR0 already has the resulting range.  */
      return;
    }

  if (vr1->type == VR_VARYING)
    {
      set_value_range_to_varying (vr0);
      return;
    }

  if (vr0->type == VR_RANGE && vr1->type == VR_RANGE)
    {
      int cmp;
      tree min, max;

      /* Compute the convex hull of the ranges.  The lower limit of
         the new range is the minimum of the two ranges.  If they
	 cannot be compared, then give up.  */
      cmp = compare_values (vr0->min, vr1->min);
      if (cmp == 0 || cmp == 1)
        min = vr1->min;
      else if (cmp == -1)
        min = vr0->min;
      else
	goto give_up;

      /* Similarly, the upper limit of the new range is the maximum
         of the two ranges.  If they cannot be compared, then
	 give up.  */
      cmp = compare_values (vr0->max, vr1->max);
      if (cmp == 0 || cmp == -1)
        max = vr1->max;
      else if (cmp == 1)
        max = vr0->max;
      else
	goto give_up;

      /* Check for useless ranges.  */
      if (INTEGRAL_TYPE_P (TREE_TYPE (min))
	  && ((vrp_val_is_min (min) || is_overflow_infinity (min))
	      && (vrp_val_is_max (max) || is_overflow_infinity (max))))
	goto give_up;

      /* The resulting set of equivalences is the intersection of
	 the two sets.  */
      if (vr0->equiv && vr1->equiv && vr0->equiv != vr1->equiv)
        bitmap_and_into (vr0->equiv, vr1->equiv);
      else if (vr0->equiv && !vr1->equiv)
        bitmap_clear (vr0->equiv);

      set_value_range (vr0, vr0->type, min, max, vr0->equiv);
    }
  else if (vr0->type == VR_ANTI_RANGE && vr1->type == VR_ANTI_RANGE)
    {
      /* Two anti-ranges meet only if their complements intersect.
         Only handle the case of identical ranges.  */
      if (compare_values (vr0->min, vr1->min) == 0
	  && compare_values (vr0->max, vr1->max) == 0
	  && compare_values (vr0->min, vr0->max) == 0)
	{
	  /* The resulting set of equivalences is the intersection of
	     the two sets.  */
	  if (vr0->equiv && vr1->equiv && vr0->equiv != vr1->equiv)
	    bitmap_and_into (vr0->equiv, vr1->equiv);
	  else if (vr0->equiv && !vr1->equiv)
	    bitmap_clear (vr0->equiv);
	}
      else
	goto give_up;
    }
  else if (vr0->type == VR_ANTI_RANGE || vr1->type == VR_ANTI_RANGE)
    {
      /* For a numeric range [VAL1, VAL2] and an anti-range ~[VAL3, VAL4],
         only handle the case where the ranges have an empty intersection.
	 The result of the meet operation is the anti-range.  */
      if (!symbolic_range_p (vr0)
	  && !symbolic_range_p (vr1)
	  && !value_ranges_intersect_p (vr0, vr1))
	{
	  /* Copy most of VR1 into VR0.  Don't copy VR1's equivalence
	     set.  We need to compute the intersection of the two
	     equivalence sets.  */
	  if (vr1->type == VR_ANTI_RANGE)
	    set_value_range (vr0, vr1->type, vr1->min, vr1->max, vr0->equiv);

	  /* The resulting set of equivalences is the intersection of
	     the two sets.  */
	  if (vr0->equiv && vr1->equiv && vr0->equiv != vr1->equiv)
	    bitmap_and_into (vr0->equiv, vr1->equiv);
	  else if (vr0->equiv && !vr1->equiv)
	    bitmap_clear (vr0->equiv);
	}
      else
	goto give_up;
    }
  else
    gcc_unreachable ();

  return;

give_up:
  /* Failed to find an efficient meet.  Before giving up and setting
     the result to VARYING, see if we can at least derive a useful
     anti-range.  FIXME, all this nonsense about distinguishing
     anti-ranges from ranges is necessary because of the odd
     semantics of range_includes_zero_p and friends.  */
  if (!symbolic_range_p (vr0)
      && ((vr0->type == VR_RANGE && !range_includes_zero_p (vr0))
	  || (vr0->type == VR_ANTI_RANGE && range_includes_zero_p (vr0)))
      && !symbolic_range_p (vr1)
      && ((vr1->type == VR_RANGE && !range_includes_zero_p (vr1))
	  || (vr1->type == VR_ANTI_RANGE && range_includes_zero_p (vr1))))
    {
      set_value_range_to_nonnull (vr0, TREE_TYPE (vr0->min));

      /* Since this meet operation did not result from the meeting of
	 two equivalent names, VR0 cannot have any equivalences.  */
      if (vr0->equiv)
	bitmap_clear (vr0->equiv);
    }
  else
    set_value_range_to_varying (vr0);
}


/* Visit all arguments for PHI node PHI that flow through executable
   edges.  If a valid value range can be derived from all the incoming
   value ranges, set a new range for the LHS of PHI.  */

static enum ssa_prop_result
vrp_visit_phi_node (GIMPLE_type phi)
{
  size_t i;
  tree lhs = PHI_RESULT (phi);
  value_range_t *lhs_vr = get_value_range (lhs);
  value_range_t vr_result = { VR_UNDEFINED, NULL_TREE, NULL_TREE, NULL };
  int edges, old_edges;
  struct loop *l;

  copy_value_range (&vr_result, lhs_vr);

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "\nVisiting PHI node: ");
      print_gimple_stmt (dump_file, phi, 0, dump_flags);
    }

  edges = 0;
  for (i = 0; i < gimple_phi_num_args (phi); i++)
    {
      edge e = gimple_phi_arg_edge (phi, i);

      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file,
	      "\n    Argument #%d (%d -> %d %sexecutable)\n",
	      (int) i, e->src->index, e->dest->index,
	      (e->flags & EDGE_EXECUTABLE) ? "" : "not ");
	}

      if (e->flags & EDGE_EXECUTABLE)
	{
	  tree arg = PHI_ARG_DEF (phi, i);
	  value_range_t vr_arg;

	  ++edges;

	  if (TREE_CODE (arg) == SSA_NAME)
	    {
	      vr_arg = *(get_value_range (arg));
	    }
	  else
	    {
	      if (is_overflow_infinity (arg))
		{
		  arg = copy_node (arg);
		  TREE_OVERFLOW (arg) = 0;
		}

	      vr_arg.type = VR_RANGE;
	      vr_arg.min = arg;
	      vr_arg.max = arg;
	      vr_arg.equiv = NULL;
	    }

	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      fprintf (dump_file, "\t");
	      print_generic_expr (dump_file, arg, dump_flags);
	      fprintf (dump_file, "\n\tValue: ");
	      dump_value_range (dump_file, &vr_arg);
	      fprintf (dump_file, "\n");
	    }

	  vrp_meet (&vr_result, &vr_arg);

	  if (vr_result.type == VR_VARYING)
	    break;
	}
    }

  /* If this is a loop PHI node SCEV may known more about its
     value-range.  */
  if (current_loops
      && (l = loop_containing_stmt (phi))
      && l->header == gimple_bb (phi))
    adjust_range_with_scev (&vr_result, l, phi, lhs);

  if (vr_result.type == VR_VARYING)
    goto varying;

  old_edges = vr_phi_edge_counts[SSA_NAME_VERSION (lhs)];
  vr_phi_edge_counts[SSA_NAME_VERSION (lhs)] = edges;

  /* To prevent infinite iterations in the algorithm, derive ranges
     when the new value is slightly bigger or smaller than the
     previous one.  We don't do this if we have seen a new executable
     edge; this helps us avoid an overflow infinity for conditionals
     which are not in a loop.  */
  if (lhs_vr->type == VR_RANGE && vr_result.type == VR_RANGE
      && edges <= old_edges)
    {
      if (!POINTER_TYPE_P (TREE_TYPE (lhs)))
	{
	  int cmp_min = compare_values (lhs_vr->min, vr_result.min);
	  int cmp_max = compare_values (lhs_vr->max, vr_result.max);

	  /* If the new minimum is smaller or larger than the previous
	     one, go all the way to -INF.  In the first case, to avoid
	     iterating millions of times to reach -INF, and in the
	     other case to avoid infinite bouncing between different
	     minimums.  */
	  if (cmp_min > 0 || cmp_min < 0)
	    {
	      /* If we will end up with a (-INF, +INF) range, set it to
		 VARYING.  Same if the previous max value was invalid for
		 the type and we'd end up with vr_result.min > vr_result.max.  */
	      if (vrp_val_is_max (vr_result.max)
		  || compare_values (TYPE_MIN_VALUE (TREE_TYPE (vr_result.min)),
				     vr_result.max) > 0)
		goto varying;

	      if (!needs_overflow_infinity (TREE_TYPE (vr_result.min))
		  || !vrp_var_may_overflow (lhs, phi))
		vr_result.min = TYPE_MIN_VALUE (TREE_TYPE (vr_result.min));
	      else if (supports_overflow_infinity (TREE_TYPE (vr_result.min)))
		vr_result.min =
		  negative_overflow_infinity (TREE_TYPE (vr_result.min));
	      else
		goto varying;
	    }

	  /* Similarly, if the new maximum is smaller or larger than
	     the previous one, go all the way to +INF.  */
	  if (cmp_max < 0 || cmp_max > 0)
	    {
	      /* If we will end up with a (-INF, +INF) range, set it to
		 VARYING.  Same if the previous min value was invalid for
		 the type and we'd end up with vr_result.max < vr_result.min.  */
	      if (vrp_val_is_min (vr_result.min)
		  || compare_values (TYPE_MAX_VALUE (TREE_TYPE (vr_result.max)),
				     vr_result.min) < 0)
		goto varying;

	      if (!needs_overflow_infinity (TREE_TYPE (vr_result.max))
		  || !vrp_var_may_overflow (lhs, phi))
		vr_result.max = TYPE_MAX_VALUE (TREE_TYPE (vr_result.max));
	      else if (supports_overflow_infinity (TREE_TYPE (vr_result.max)))
		vr_result.max =
		  positive_overflow_infinity (TREE_TYPE (vr_result.max));
	      else
		goto varying;
	    }
	}
    }

  /* If the new range is different than the previous value, keep
     iterating.  */
  if (update_value_range (lhs, &vr_result))
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file, "Found new range for ");
	  print_generic_expr (dump_file, lhs, 0);
	  fprintf (dump_file, ": ");
	  dump_value_range (dump_file, &vr_result);
	  fprintf (dump_file, "\n\n");
	}

      return SSA_PROP_INTERESTING;
    }

  /* Nothing changed, don't add outgoing edges.  */
  return SSA_PROP_NOT_INTERESTING;

  /* No match found.  Set the LHS to VARYING.  */
varying:
  set_value_range_to_varying (lhs_vr);
  return SSA_PROP_VARYING;
}
#elif (GCC_VERSION == 4006)
/* Return the maximum value for TYPE.  */

static inline tree
vrp_val_max (const_tree type)
{
  if (!INTEGRAL_TYPE_P (type))
    return NULL_TREE;

  return TYPE_MAX_VALUE (type);
}

/* Return the minimum value for TYPE.  */

static inline tree
vrp_val_min (const_tree type)
{
  if (!INTEGRAL_TYPE_P (type))
    return NULL_TREE;

  return TYPE_MIN_VALUE (type);
}

/* Return whether VAL is equal to the maximum value of its type.  This
   will be true for a positive overflow infinity.  We can't do a
   simple equality comparison with TYPE_MAX_VALUE because C typedefs
   and Ada subtypes can produce types whose TYPE_MAX_VALUE is not ==
   to the integer constant with the same value in the type.  */

static inline bool
vrp_val_is_max (const_tree val)
{
  tree type_max = vrp_val_max (TREE_TYPE (val));
  return (val == type_max
	  || (type_max != NULL_TREE
	      && operand_equal_p (val, type_max, 0)));
}

/* Return whether VAL is equal to the minimum value of its type.  This
   will be true for a negative overflow infinity.  */

static inline bool
vrp_val_is_min (const_tree val)
{
  tree type_min = vrp_val_min (TREE_TYPE (val));
  return (val == type_min
	  || (type_min != NULL_TREE
	      && operand_equal_p (val, type_min, 0)));
}


/* Return whether TYPE should use an overflow infinity distinct from
   TYPE_{MIN,MAX}_VALUE.  We use an overflow infinity value to
   represent a signed overflow during VRP computations.  An infinity
   is distinct from a half-range, which will go from some number to
   TYPE_{MIN,MAX}_VALUE.  */

static inline bool
needs_overflow_infinity (const_tree type)
{
  return INTEGRAL_TYPE_P (type) && !TYPE_OVERFLOW_WRAPS (type);
}

/* Return whether TYPE can support our overflow infinity
   representation: we use the TREE_OVERFLOW flag, which only exists
   for constants.  If TYPE doesn't support this, we don't optimize
   cases which would require signed overflow--we drop them to
   VARYING.  */

static inline bool
supports_overflow_infinity (const_tree type)
{
  tree min = vrp_val_min (type), max = vrp_val_max (type);
#ifdef ENABLE_CHECKING
  gcc_assert (needs_overflow_infinity (type));
#endif
  return (min != NULL_TREE
	  && CONSTANT_CLASS_P (min)
	  && max != NULL_TREE
	  && CONSTANT_CLASS_P (max));
}

/* VAL is the maximum or minimum value of a type.  Return a
   corresponding overflow infinity.  */

static inline tree
make_overflow_infinity (tree val)
{
  gcc_checking_assert (val != NULL_TREE && CONSTANT_CLASS_P (val));
  val = copy_node (val);
  TREE_OVERFLOW (val) = 1;
  return val;
}

/* Return a negative overflow infinity for TYPE.  */

static inline tree
negative_overflow_infinity (tree type)
{
  gcc_checking_assert (supports_overflow_infinity (type));
  return make_overflow_infinity (vrp_val_min (type));
}

/* Return a positive overflow infinity for TYPE.  */

static inline tree
positive_overflow_infinity (tree type)
{
  gcc_checking_assert (supports_overflow_infinity (type));
  return make_overflow_infinity (vrp_val_max (type));
}

/* Return whether VAL is a negative overflow infinity.  */

static inline bool
is_negative_overflow_infinity (const_tree val)
{
  return (needs_overflow_infinity (TREE_TYPE (val))
	  && CONSTANT_CLASS_P (val)
	  && TREE_OVERFLOW (val)
	  && vrp_val_is_min (val));
}

/* Return whether VAL is a positive overflow infinity.  */

static inline bool
is_positive_overflow_infinity (const_tree val)
{
  return (needs_overflow_infinity (TREE_TYPE (val))
	  && CONSTANT_CLASS_P (val)
	  && TREE_OVERFLOW (val)
	  && vrp_val_is_max (val));
}

/* Return whether VAL is a positive or negative overflow infinity.  */

static inline bool
is_overflow_infinity (const_tree val)
{
  return (needs_overflow_infinity (TREE_TYPE (val))
	  && CONSTANT_CLASS_P (val)
	  && TREE_OVERFLOW (val)
	  && (vrp_val_is_min (val) || vrp_val_is_max (val)));
}

/* Return whether STMT has a constant rhs that is_overflow_infinity. */

static inline bool
stmt_overflow_infinity (GIMPLE_type stmt)
{
  if (is_gimple_assign (stmt)
      && get_gimple_rhs_class (gimple_assign_rhs_code (stmt)) ==
      GIMPLE_SINGLE_RHS)
    return is_overflow_infinity (gimple_assign_rhs1 (stmt));
  return false;
}

/* If VAL is now an overflow infinity, return VAL.  Otherwise, return
   the same value with TREE_OVERFLOW clear.  This can be used to avoid
   confusing a regular value with an overflow value.  */

static inline tree
avoid_overflow_infinity (tree val)
{
  if (!is_overflow_infinity (val))
    return val;

  if (vrp_val_is_max (val))
    return vrp_val_max (TREE_TYPE (val));
  else
    {
      gcc_checking_assert (vrp_val_is_min (val));
      return vrp_val_min (TREE_TYPE (val));
    }
}


/* Return true if ARG is marked with the nonnull attribute in the
   current function signature.  */

static bool
nonnull_arg_p (const_tree arg)
{
  tree t, attrs, fntype;
  unsigned HOST_WIDE_INT arg_num;

  gcc_assert (TREE_CODE (arg) == PARM_DECL && POINTER_TYPE_P (TREE_TYPE (arg)));

  /* The static chain decl is always non null.  */
  if (arg == cfun->static_chain_decl)
    return true;

  fntype = TREE_TYPE (current_function_decl);
  attrs = lookup_attribute ("nonnull", TYPE_ATTRIBUTES (fntype));

  /* If "nonnull" wasn't specified, we know nothing about the argument.  */
  if (attrs == NULL_TREE)
    return false;

  /* If "nonnull" applies to all the arguments, then ARG is non-null.  */
  if (TREE_VALUE (attrs) == NULL_TREE)
    return true;

  /* Get the position number for ARG in the function signature.  */
  for (arg_num = 1, t = DECL_ARGUMENTS (current_function_decl);
       t;
       t = DECL_CHAIN (t), arg_num++)
    {
      if (t == arg)
	break;
    }

  gcc_assert (t == arg);

  /* Now see if ARG_NUM is mentioned in the nonnull list.  */
  for (t = TREE_VALUE (attrs); t; t = TREE_CHAIN (t))
    {
      if (compare_tree_int (TREE_VALUE (t), arg_num) == 0)
	return true;
    }

  return false;
}


/* Set value range VR to VR_VARYING.  */

static inline void
set_value_range_to_varying (value_range_t *vr)
{
  vr->type = VR_VARYING;
  vr->min = vr->max = NULL_TREE;
  if (vr->equiv)
    bitmap_clear (vr->equiv);
}


/* Set value range VR to {T, MIN, MAX, EQUIV}.  */

static void
set_value_range (value_range_t *vr, enum value_range_type t, tree min,
		 tree max, bitmap equiv)
{
#if defined ENABLE_CHECKING
  /* Check the validity of the range.  */
  if (t == VR_RANGE || t == VR_ANTI_RANGE)
    {
      int cmp;

      gcc_assert (min && max);

      if (INTEGRAL_TYPE_P (TREE_TYPE (min)) && t == VR_ANTI_RANGE)
	gcc_assert (!vrp_val_is_min (min) || !vrp_val_is_max (max));

      cmp = compare_values (min, max);
      gcc_assert (cmp == 0 || cmp == -1 || cmp == -2);

      if (needs_overflow_infinity (TREE_TYPE (min)))
	gcc_assert (!is_overflow_infinity (min)
		    || !is_overflow_infinity (max));
    }

  if (t == VR_UNDEFINED || t == VR_VARYING)
    gcc_assert (min == NULL_TREE && max == NULL_TREE);

  if (t == VR_UNDEFINED || t == VR_VARYING)
    gcc_assert (equiv == NULL || bitmap_empty_p (equiv));
#endif

  vr->type = t;
  vr->min = min;
  vr->max = max;

  /* Since updating the equivalence set involves deep copying the
     bitmaps, only do it if absolutely necessary.  */
  if (vr->equiv == NULL
      && equiv != NULL)
    vr->equiv = BITMAP_ALLOC (NULL);

  if (equiv != vr->equiv)
    {
      if (equiv && !bitmap_empty_p (equiv))
	bitmap_copy (vr->equiv, equiv);
      else
	bitmap_clear (vr->equiv);
    }
}


/* Set value range VR to the canonical form of {T, MIN, MAX, EQUIV}.
   This means adjusting T, MIN and MAX representing the case of a
   wrapping range with MAX < MIN covering [MIN, type_max] U [type_min, MAX]
   as anti-rage ~[MAX+1, MIN-1].  Likewise for wrapping anti-ranges.
   In corner cases where MAX+1 or MIN-1 wraps this will fall back
   to varying.
   This routine exists to ease canonicalization in the case where we
   extract ranges from var + CST op limit.  */

static void
set_and_canonicalize_value_range (value_range_t *vr, enum value_range_type t,
				  tree min, tree max, bitmap equiv)
{
  /* Nothing to canonicalize for symbolic or unknown or varying ranges.  */
  if ((t != VR_RANGE
       && t != VR_ANTI_RANGE)
      || TREE_CODE (min) != INTEGER_CST
      || TREE_CODE (max) != INTEGER_CST)
    {
      set_value_range (vr, t, min, max, equiv);
      return;
    }

  /* Wrong order for min and max, to swap them and the VR type we need
     to adjust them.  */
  if (tree_int_cst_lt (max, min))
    {
      tree one = build_int_cst (TREE_TYPE (min), 1);
      tree tmp = int_const_binop (PLUS_EXPR, max, one, 0);
      max = int_const_binop (MINUS_EXPR, min, one, 0);
      min = tmp;

      /* There's one corner case, if we had [C+1, C] before we now have
	 that again.  But this represents an empty value range, so drop
	 to varying in this case.  */
      if (tree_int_cst_lt (max, min))
	{
	  set_value_range_to_varying (vr);
	  return;
	}

      t = t == VR_RANGE ? VR_ANTI_RANGE : VR_RANGE;
    }

  /* Anti-ranges that can be represented as ranges should be so.  */
  if (t == VR_ANTI_RANGE)
    {
      bool is_min = vrp_val_is_min (min);
      bool is_max = vrp_val_is_max (max);

      if (is_min && is_max)
	{
	  /* We cannot deal with empty ranges, drop to varying.  */
	  set_value_range_to_varying (vr);
	  return;
	}
      else if (is_min
	       /* As a special exception preserve non-null ranges.  */
	       && !(TYPE_UNSIGNED (TREE_TYPE (min))
		    && integer_zerop (max)))
        {
	  tree one = build_int_cst (TREE_TYPE (max), 1);
	  min = int_const_binop (PLUS_EXPR, max, one, 0);
	  max = vrp_val_max (TREE_TYPE (max));
	  t = VR_RANGE;
        }
      else if (is_max)
        {
	  tree one = build_int_cst (TREE_TYPE (min), 1);
	  max = int_const_binop (MINUS_EXPR, min, one, 0);
	  min = vrp_val_min (TREE_TYPE (min));
	  t = VR_RANGE;
        }
    }

  set_value_range (vr, t, min, max, equiv);
}

/* Copy value range FROM into value range TO.  */

static inline void
copy_value_range (value_range_t *to, value_range_t *from)
{
  set_value_range (to, from->type, from->min, from->max, from->equiv);
}

/* Set value range VR to a single value.  This function is only called
   with values we get from statements, and exists to clear the
   TREE_OVERFLOW flag so that we don't think we have an overflow
   infinity when we shouldn't.  */

static inline void
set_value_range_to_value (value_range_t *vr, tree val, bitmap equiv)
{
  gcc_assert (is_gimple_min_invariant (val));
  val = avoid_overflow_infinity (val);
  set_value_range (vr, VR_RANGE, val, val, equiv);
}

/* Set value range VR to a non-negative range of type TYPE.
   OVERFLOW_INFINITY indicates whether to use an overflow infinity
   rather than TYPE_MAX_VALUE; this should be true if we determine
   that the range is nonnegative based on the assumption that signed
   overflow does not occur.  */

static inline void
set_value_range_to_nonnegative (value_range_t *vr, tree type,
				bool overflow_infinity)
{
  tree zero;

  if (overflow_infinity && !supports_overflow_infinity (type))
    {
      set_value_range_to_varying (vr);
      return;
    }

  zero = build_int_cst (type, 0);
  set_value_range (vr, VR_RANGE, zero,
		   (overflow_infinity
		    ? positive_overflow_infinity (type)
		    : TYPE_MAX_VALUE (type)),
		   vr->equiv);
}

/* Set value range VR to a non-NULL range of type TYPE.  */

static inline void
set_value_range_to_nonnull (value_range_t *vr, tree type)
{
  tree zero = build_int_cst (type, 0);
  set_value_range (vr, VR_ANTI_RANGE, zero, zero, vr->equiv);
}


/* Set value range VR to a NULL range of type TYPE.  */

static inline void
set_value_range_to_null (value_range_t *vr, tree type)
{
  set_value_range_to_value (vr, build_int_cst (type, 0), vr->equiv);
}


/* Set value range VR to a range of a truthvalue of type TYPE.  */

static inline void
set_value_range_to_truthvalue (value_range_t *vr, tree type)
{
  if (TYPE_PRECISION (type) == 1)
    set_value_range_to_varying (vr);
  else
    set_value_range (vr, VR_RANGE,
		     build_int_cst (type, 0), build_int_cst (type, 1),
		     vr->equiv);
}


/* Set value range VR to VR_UNDEFINED.  */

static inline void
set_value_range_to_undefined (value_range_t *vr)
{
  vr->type = VR_UNDEFINED;
  vr->min = vr->max = NULL_TREE;
  if (vr->equiv)
    bitmap_clear (vr->equiv);
}


/* If abs (min) < abs (max), set VR to [-max, max], if
   abs (min) >= abs (max), set VR to [-min, min].  */

static void
abs_extent_range (value_range_t *vr, tree min, tree max)
{
  int cmp;

  gcc_assert (TREE_CODE (min) == INTEGER_CST);
  gcc_assert (TREE_CODE (max) == INTEGER_CST);
  gcc_assert (INTEGRAL_TYPE_P (TREE_TYPE (min)));
  gcc_assert (!TYPE_UNSIGNED (TREE_TYPE (min)));
  min = fold_unary (ABS_EXPR, TREE_TYPE (min), min);
  max = fold_unary (ABS_EXPR, TREE_TYPE (max), max);
  if (TREE_OVERFLOW (min) || TREE_OVERFLOW (max))
    {
      set_value_range_to_varying (vr);
      return;
    }
  cmp = compare_values (min, max);
  if (cmp == -1)
    min = fold_unary (NEGATE_EXPR, TREE_TYPE (min), max);
  else if (cmp == 0 || cmp == 1)
    {
      max = min;
      min = fold_unary (NEGATE_EXPR, TREE_TYPE (min), min);
    }
  else
    {
      set_value_range_to_varying (vr);
      return;
    }
  set_and_canonicalize_value_range (vr, VR_RANGE, min, max, NULL);
}


/* Return value range information for VAR.

   If we have no values ranges recorded (ie, VRP is not running), then
   return NULL.  Otherwise create an empty range if none existed for VAR.  */

static value_range_t *
get_value_range (const_tree var)
{
  value_range_t *vr;
  tree sym;
  unsigned ver = SSA_NAME_VERSION (var);

  /* If we have no recorded ranges, then return NULL.  */
  if (! vr_value)
    return NULL;

  vr = vr_value[ver];
  if (vr)
    return vr;

  /* Create a default value range.  */
  vr_value[ver] = vr = XCNEW (value_range_t);

  /* Defer allocating the equivalence set.  */
  vr->equiv = NULL;

  /* If VAR is a default definition, the variable can take any value
     in VAR's type.  */
  sym = SSA_NAME_VAR (var);
  if (SSA_NAME_IS_DEFAULT_DEF (var))
    {
      /* Try to use the "nonnull" attribute to create ~[0, 0]
	 anti-ranges for pointers.  Note that this is only valid with
	 default definitions of PARM_DECLs.  */
      if (TREE_CODE (sym) == PARM_DECL
	  && POINTER_TYPE_P (TREE_TYPE (sym))
	  && nonnull_arg_p (sym))
	set_value_range_to_nonnull (vr, TREE_TYPE (sym));
      else
	set_value_range_to_varying (vr);
    }

  return vr;
}

/* Return true, if VAL1 and VAL2 are equal values for VRP purposes.  */

static inline bool
vrp_operand_equal_p (const_tree val1, const_tree val2)
{
  if (val1 == val2)
    return true;
  if (!val1 || !val2 || !operand_equal_p (val1, val2, 0))
    return false;
  if (is_overflow_infinity (val1))
    return is_overflow_infinity (val2);
  return true;
}

/* Return true, if the bitmaps B1 and B2 are equal.  */

static inline bool
vrp_bitmap_equal_p (const_bitmap b1, const_bitmap b2)
{
  return (b1 == b2
	  || ((!b1 || bitmap_empty_p (b1))
	      && (!b2 || bitmap_empty_p (b2)))
	  || (b1 && b2
	      && bitmap_equal_p (b1, b2)));
}

/* Update the value range and equivalence set for variable VAR to
   NEW_VR.  Return true if NEW_VR is different from VAR's previous
   value.

   NOTE: This function assumes that NEW_VR is a temporary value range
   object created for the sole purpose of updating VAR's range.  The
   storage used by the equivalence set from NEW_VR will be freed by
   this function.  Do not call update_value_range when NEW_VR
   is the range object associated with another SSA name.  */

static inline bool
update_value_range (const_tree var, value_range_t *new_vr)
{
  value_range_t *old_vr;
  bool is_new;

  /* Update the value range, if necessary.  */
  old_vr = get_value_range (var);
  is_new = old_vr->type != new_vr->type
	   || !vrp_operand_equal_p (old_vr->min, new_vr->min)
	   || !vrp_operand_equal_p (old_vr->max, new_vr->max)
	   || !vrp_bitmap_equal_p (old_vr->equiv, new_vr->equiv);

  if (is_new)
    set_value_range (old_vr, new_vr->type, new_vr->min, new_vr->max,
	             new_vr->equiv);

  BITMAP_FREE (new_vr->equiv);

  return is_new;
}


/* Add VAR and VAR's equivalence set to EQUIV.  This is the central
   point where equivalence processing can be turned on/off.  */

static void
add_equivalence (bitmap *equiv, const_tree var)
{
  unsigned ver = SSA_NAME_VERSION (var);
  value_range_t *vr = vr_value[ver];

  if (*equiv == NULL)
    *equiv = BITMAP_ALLOC (NULL);
  bitmap_set_bit (*equiv, ver);
  if (vr && vr->equiv)
    bitmap_ior_into (*equiv, vr->equiv);
}


/* Return true if VR is ~[0, 0].  */

static inline bool
range_is_nonnull (value_range_t *vr)
{
  return vr->type == VR_ANTI_RANGE
	 && integer_zerop (vr->min)
	 && integer_zerop (vr->max);
}


/* Return true if VR is [0, 0].  */

static inline bool
range_is_null (value_range_t *vr)
{
  return vr->type == VR_RANGE
	 && integer_zerop (vr->min)
	 && integer_zerop (vr->max);
}

/* Return true if max and min of VR are INTEGER_CST.  It's not necessary
   a singleton.  */

static inline bool
range_int_cst_p (value_range_t *vr)
{
  return (vr->type == VR_RANGE
	  && TREE_CODE (vr->max) == INTEGER_CST
	  && TREE_CODE (vr->min) == INTEGER_CST
	  && !TREE_OVERFLOW (vr->max)
	  && !TREE_OVERFLOW (vr->min));
}

/* Return true if VR is a INTEGER_CST singleton.  */

static inline bool
range_int_cst_singleton_p (value_range_t *vr)
{
  return (range_int_cst_p (vr)
	  && tree_int_cst_equal (vr->min, vr->max));
}

/* Return true if value range VR involves at least one symbol.  */

static inline bool
symbolic_range_p (value_range_t *vr)
{
  return (!is_gimple_min_invariant (vr->min)
          || !is_gimple_min_invariant (vr->max));
}

/* Return true if value range VR uses an overflow infinity.  */

static inline bool
overflow_infinity_range_p (value_range_t *vr)
{
  return (vr->type == VR_RANGE
	  && (is_overflow_infinity (vr->min)
	      || is_overflow_infinity (vr->max)));
}

/* Return false if we can not make a valid comparison based on VR;
   this will be the case if it uses an overflow infinity and overflow
   is not undefined (i.e., -fno-strict-overflow is in effect).
   Otherwise return true, and set *STRICT_OVERFLOW_P to true if VR
   uses an overflow infinity.  */

static bool
usable_range_p (value_range_t *vr, bool *strict_overflow_p)
{
  gcc_assert (vr->type == VR_RANGE);
  if (is_overflow_infinity (vr->min))
    {
      *strict_overflow_p = true;
      if (!TYPE_OVERFLOW_UNDEFINED (TREE_TYPE (vr->min)))
	return false;
    }
  if (is_overflow_infinity (vr->max))
    {
      *strict_overflow_p = true;
      if (!TYPE_OVERFLOW_UNDEFINED (TREE_TYPE (vr->max)))
	return false;
    }
  return true;
}


/* Like tree_expr_nonnegative_warnv_p, but this function uses value
   ranges obtained so far.  */

static bool
vrp_expr_computes_nonnegative (tree expr, bool *strict_overflow_p)
{
  return (tree_expr_nonnegative_warnv_p (expr, strict_overflow_p)
	  || (TREE_CODE (expr) == SSA_NAME
	      && ssa_name_nonnegative_p (expr)));
}

/* Return true if the result of assignment STMT is know to be non-negative.
   If the return value is based on the assumption that signed overflow is
   undefined, set *STRICT_OVERFLOW_P to true; otherwise, don't change
   *STRICT_OVERFLOW_P.*/

static bool
gimple_assign_nonnegative_warnv_p (GIMPLE_type stmt, bool *strict_overflow_p)
{
  enum tree_code code = gimple_assign_rhs_code (stmt);
  switch (get_gimple_rhs_class (code))
    {
    case GIMPLE_UNARY_RHS:
      return tree_unary_nonnegative_warnv_p (gimple_assign_rhs_code (stmt),
					     gimple_expr_type (stmt),
					     gimple_assign_rhs1 (stmt),
					     strict_overflow_p);
    case GIMPLE_BINARY_RHS:
      return tree_binary_nonnegative_warnv_p (gimple_assign_rhs_code (stmt),
					      gimple_expr_type (stmt),
					      gimple_assign_rhs1 (stmt),
					      gimple_assign_rhs2 (stmt),
					      strict_overflow_p);
    case GIMPLE_TERNARY_RHS:
      return false;
    case GIMPLE_SINGLE_RHS:
      return tree_single_nonnegative_warnv_p (gimple_assign_rhs1 (stmt),
					      strict_overflow_p);
    case GIMPLE_INVALID_RHS:
      gcc_unreachable ();
    default:
      gcc_unreachable ();
    }
}

/* Return true if return value of call STMT is know to be non-negative.
   If the return value is based on the assumption that signed overflow is
   undefined, set *STRICT_OVERFLOW_P to true; otherwise, don't change
   *STRICT_OVERFLOW_P.*/

static bool
gimple_call_nonnegative_warnv_p (GIMPLE_type stmt, bool *strict_overflow_p)
{
  tree arg0 = gimple_call_num_args (stmt) > 0 ?
    gimple_call_arg (stmt, 0) : NULL_TREE;
  tree arg1 = gimple_call_num_args (stmt) > 1 ?
    gimple_call_arg (stmt, 1) : NULL_TREE;

  return tree_call_nonnegative_warnv_p (gimple_expr_type (stmt),
					gimple_call_fndecl (stmt),
					arg0,
					arg1,
					strict_overflow_p);
}

/* Return true if STMT is know to to compute a non-negative value.
   If the return value is based on the assumption that signed overflow is
   undefined, set *STRICT_OVERFLOW_P to true; otherwise, don't change
   *STRICT_OVERFLOW_P.*/

static bool
gimple_stmt_nonnegative_warnv_p (GIMPLE_type stmt, bool *strict_overflow_p)
{
  switch (gimple_code (stmt))
    {
    case GIMPLE_ASSIGN:
      return gimple_assign_nonnegative_warnv_p (stmt, strict_overflow_p);
    case GIMPLE_CALL:
      return gimple_call_nonnegative_warnv_p (stmt, strict_overflow_p);
    default:
      gcc_unreachable ();
    }
}

/* Return true if the result of assignment STMT is know to be non-zero.
   If the return value is based on the assumption that signed overflow is
   undefined, set *STRICT_OVERFLOW_P to true; otherwise, don't change
   *STRICT_OVERFLOW_P.*/

static bool
gimple_assign_nonzero_warnv_p (GIMPLE_type stmt, bool *strict_overflow_p)
{
  enum tree_code code = gimple_assign_rhs_code (stmt);
  switch (get_gimple_rhs_class (code))
    {
    case GIMPLE_UNARY_RHS:
      return tree_unary_nonzero_warnv_p (gimple_assign_rhs_code (stmt),
					 gimple_expr_type (stmt),
					 gimple_assign_rhs1 (stmt),
					 strict_overflow_p);
    case GIMPLE_BINARY_RHS:
      return tree_binary_nonzero_warnv_p (gimple_assign_rhs_code (stmt),
					  gimple_expr_type (stmt),
					  gimple_assign_rhs1 (stmt),
					  gimple_assign_rhs2 (stmt),
					  strict_overflow_p);
    case GIMPLE_TERNARY_RHS:
      return false;
    case GIMPLE_SINGLE_RHS:
      return tree_single_nonzero_warnv_p (gimple_assign_rhs1 (stmt),
					  strict_overflow_p);
    case GIMPLE_INVALID_RHS:
      gcc_unreachable ();
    default:
      gcc_unreachable ();
    }
}

/* Return true if STMT is know to to compute a non-zero value.
   If the return value is based on the assumption that signed overflow is
   undefined, set *STRICT_OVERFLOW_P to true; otherwise, don't change
   *STRICT_OVERFLOW_P.*/

static bool
gimple_stmt_nonzero_warnv_p (GIMPLE_type stmt, bool *strict_overflow_p)
{
  switch (gimple_code (stmt))
    {
    case GIMPLE_ASSIGN:
      return gimple_assign_nonzero_warnv_p (stmt, strict_overflow_p);
    case GIMPLE_CALL:
      return gimple_alloca_call_p (stmt);
    default:
      gcc_unreachable ();
    }
}

/* Like tree_expr_nonzero_warnv_p, but this function uses value ranges
   obtained so far.  */

static bool
vrp_stmt_computes_nonzero (GIMPLE_type stmt, bool *strict_overflow_p)
{
  if (gimple_stmt_nonzero_warnv_p (stmt, strict_overflow_p))
    return true;

  /* If we have an expression of the form &X->a, then the expression
     is nonnull if X is nonnull.  */
  if (is_gimple_assign (stmt)
      && gimple_assign_rhs_code (stmt) == ADDR_EXPR)
    {
      tree expr = gimple_assign_rhs1 (stmt);
      tree base = get_base_address (TREE_OPERAND (expr, 0));

      if (base != NULL_TREE
	  && TREE_CODE (base) == MEM_REF
	  && TREE_CODE (TREE_OPERAND (base, 0)) == SSA_NAME)
	{
	  value_range_t *vr = get_value_range (TREE_OPERAND (base, 0));
	  if (range_is_nonnull (vr))
	    return true;
	}
    }

  return false;
}

/* Returns true if EXPR is a valid value (as expected by compare_values) --
   a GIMPLE_type invariant, or SSA_NAME +- CST.  */

static bool
valid_value_p (tree expr)
{
  if (TREE_CODE (expr) == SSA_NAME)
    return true;

  if (TREE_CODE (expr) == PLUS_EXPR
      || TREE_CODE (expr) == MINUS_EXPR)
    return (TREE_CODE (TREE_OPERAND (expr, 0)) == SSA_NAME
	    && TREE_CODE (TREE_OPERAND (expr, 1)) == INTEGER_CST);

  return is_gimple_min_invariant (expr);
}

/* Return
   1 if VAL < VAL2
   0 if !(VAL < VAL2)
   -2 if those are incomparable.  */
static inline int
operand_less_p (tree val, tree val2)
{
  /* LT is folded faster than GE and others.  Inline the common case.  */
  if (TREE_CODE (val) == INTEGER_CST && TREE_CODE (val2) == INTEGER_CST)
    {
      if (TYPE_UNSIGNED (TREE_TYPE (val)))
	return INT_CST_LT_UNSIGNED (val, val2);
      else
	{
	  if (INT_CST_LT (val, val2))
	    return 1;
	}
    }
  else
    {
      tree tcmp;

      fold_defer_overflow_warnings ();

      tcmp = fold_binary_to_constant (LT_EXPR, boolean_type_node, val, val2);

      fold_undefer_and_ignore_overflow_warnings ();

      if (!tcmp
	  || TREE_CODE (tcmp) != INTEGER_CST)
	return -2;

      if (!integer_zerop (tcmp))
	return 1;
    }

  /* val >= val2, not considering overflow infinity.  */
  if (is_negative_overflow_infinity (val))
    return is_negative_overflow_infinity (val2) ? 0 : 1;
  else if (is_positive_overflow_infinity (val2))
    return is_positive_overflow_infinity (val) ? 0 : 1;

  return 0;
}

/* Compare two values VAL1 and VAL2.  Return

   	-2 if VAL1 and VAL2 cannot be compared at compile-time,
   	-1 if VAL1 < VAL2,
   	 0 if VAL1 == VAL2,
	+1 if VAL1 > VAL2, and
	+2 if VAL1 != VAL2

   This is similar to tree_int_cst_compare but supports pointer values
   and values that cannot be compared at compile time.

   If STRICT_OVERFLOW_P is not NULL, then set *STRICT_OVERFLOW_P to
   true if the return value is only valid if we assume that signed
   overflow is undefined.  */

static int
compare_values_warnv (tree val1, tree val2, bool *strict_overflow_p)
{
  if (val1 == val2)
    return 0;

  /* Below we rely on the fact that VAL1 and VAL2 are both pointers or
     both integers.  */
  gcc_assert (POINTER_TYPE_P (TREE_TYPE (val1))
	      == POINTER_TYPE_P (TREE_TYPE (val2)));
  /* Convert the two values into the same type.  This is needed because
     sizetype causes sign extension even for unsigned types.  */
  val2 = fold_convert (TREE_TYPE (val1), val2);
  STRIP_USELESS_TYPE_CONVERSION (val2);

  if ((TREE_CODE (val1) == SSA_NAME
       || TREE_CODE (val1) == PLUS_EXPR
       || TREE_CODE (val1) == MINUS_EXPR)
      && (TREE_CODE (val2) == SSA_NAME
	  || TREE_CODE (val2) == PLUS_EXPR
	  || TREE_CODE (val2) == MINUS_EXPR))
    {
      tree n1, c1, n2, c2;
      enum tree_code code1, code2;

      /* If VAL1 and VAL2 are of the form 'NAME [+-] CST' or 'NAME',
	 return -1 or +1 accordingly.  If VAL1 and VAL2 don't use the
	 same name, return -2.  */
      if (TREE_CODE (val1) == SSA_NAME)
	{
	  code1 = SSA_NAME;
	  n1 = val1;
	  c1 = NULL_TREE;
	}
      else
	{
	  code1 = TREE_CODE (val1);
	  n1 = TREE_OPERAND (val1, 0);
	  c1 = TREE_OPERAND (val1, 1);
	  if (tree_int_cst_sgn (c1) == -1)
	    {
	      if (is_negative_overflow_infinity (c1))
		return -2;
	      c1 = fold_unary_to_constant (NEGATE_EXPR, TREE_TYPE (c1), c1);
	      if (!c1)
		return -2;
	      code1 = code1 == MINUS_EXPR ? PLUS_EXPR : MINUS_EXPR;
	    }
	}

      if (TREE_CODE (val2) == SSA_NAME)
	{
	  code2 = SSA_NAME;
	  n2 = val2;
	  c2 = NULL_TREE;
	}
      else
	{
	  code2 = TREE_CODE (val2);
	  n2 = TREE_OPERAND (val2, 0);
	  c2 = TREE_OPERAND (val2, 1);
	  if (tree_int_cst_sgn (c2) == -1)
	    {
	      if (is_negative_overflow_infinity (c2))
		return -2;
	      c2 = fold_unary_to_constant (NEGATE_EXPR, TREE_TYPE (c2), c2);
	      if (!c2)
		return -2;
	      code2 = code2 == MINUS_EXPR ? PLUS_EXPR : MINUS_EXPR;
	    }
	}

      /* Both values must use the same name.  */
      if (n1 != n2)
	return -2;

      if (code1 == SSA_NAME
	  && code2 == SSA_NAME)
	/* NAME == NAME  */
	return 0;

      /* If overflow is defined we cannot simplify more.  */
      if (!TYPE_OVERFLOW_UNDEFINED (TREE_TYPE (val1)))
	return -2;

      if (strict_overflow_p != NULL
	  && (code1 == SSA_NAME || !TREE_NO_WARNING (val1))
	  && (code2 == SSA_NAME || !TREE_NO_WARNING (val2)))
	*strict_overflow_p = true;

      if (code1 == SSA_NAME)
	{
	  if (code2 == PLUS_EXPR)
	    /* NAME < NAME + CST  */
	    return -1;
	  else if (code2 == MINUS_EXPR)
	    /* NAME > NAME - CST  */
	    return 1;
	}
      else if (code1 == PLUS_EXPR)
	{
	  if (code2 == SSA_NAME)
	    /* NAME + CST > NAME  */
	    return 1;
	  else if (code2 == PLUS_EXPR)
	    /* NAME + CST1 > NAME + CST2, if CST1 > CST2  */
	    return compare_values_warnv (c1, c2, strict_overflow_p);
	  else if (code2 == MINUS_EXPR)
	    /* NAME + CST1 > NAME - CST2  */
	    return 1;
	}
      else if (code1 == MINUS_EXPR)
	{
	  if (code2 == SSA_NAME)
	    /* NAME - CST < NAME  */
	    return -1;
	  else if (code2 == PLUS_EXPR)
	    /* NAME - CST1 < NAME + CST2  */
	    return -1;
	  else if (code2 == MINUS_EXPR)
	    /* NAME - CST1 > NAME - CST2, if CST1 < CST2.  Notice that
	       C1 and C2 are swapped in the call to compare_values.  */
	    return compare_values_warnv (c2, c1, strict_overflow_p);
	}

      gcc_unreachable ();
    }

  /* We cannot compare non-constants.  */
  if (!is_gimple_min_invariant (val1) || !is_gimple_min_invariant (val2))
    return -2;

  if (!POINTER_TYPE_P (TREE_TYPE (val1)))
    {
      /* We cannot compare overflowed values, except for overflow
	 infinities.  */
      if (TREE_OVERFLOW (val1) || TREE_OVERFLOW (val2))
	{
	  if (strict_overflow_p != NULL)
	    *strict_overflow_p = true;
	  if (is_negative_overflow_infinity (val1))
	    return is_negative_overflow_infinity (val2) ? 0 : -1;
	  else if (is_negative_overflow_infinity (val2))
	    return 1;
	  else if (is_positive_overflow_infinity (val1))
	    return is_positive_overflow_infinity (val2) ? 0 : 1;
	  else if (is_positive_overflow_infinity (val2))
	    return -1;
	  return -2;
	}

      return tree_int_cst_compare (val1, val2);
    }
  else
    {
      tree t;

      /* First see if VAL1 and VAL2 are not the same.  */
      if (val1 == val2 || operand_equal_p (val1, val2, 0))
	return 0;

      /* If VAL1 is a lower address than VAL2, return -1.  */
      if (operand_less_p (val1, val2) == 1)
	return -1;

      /* If VAL1 is a higher address than VAL2, return +1.  */
      if (operand_less_p (val2, val1) == 1)
	return 1;

      /* If VAL1 is different than VAL2, return +2.
	 For integer constants we either have already returned -1 or 1
	 or they are equivalent.  We still might succeed in proving
	 something about non-trivial operands.  */
      if (TREE_CODE (val1) != INTEGER_CST
	  || TREE_CODE (val2) != INTEGER_CST)
	{
          t = fold_binary_to_constant (NE_EXPR, boolean_type_node, val1, val2);
	  if (t && integer_onep (t))
	    return 2;
	}

      return -2;
    }
}

/* Compare values like compare_values_warnv, but treat comparisons of
   nonconstants which rely on undefined overflow as incomparable.  */

static int
compare_values (tree val1, tree val2)
{
  bool sop;
  int ret;

  sop = false;
  ret = compare_values_warnv (val1, val2, &sop);
  if (sop
      && (!is_gimple_min_invariant (val1) || !is_gimple_min_invariant (val2)))
    ret = -2;
  return ret;
}


/* Return 1 if VAL is inside value range VR (VR->MIN <= VAL <= VR->MAX),
          0 if VAL is not inside VR,
	 -2 if we cannot tell either way.

   FIXME, the current semantics of this functions are a bit quirky
	  when taken in the context of VRP.  In here we do not care
	  about VR's type.  If VR is the anti-range ~[3, 5] the call
	  value_inside_range (4, VR) will return 1.

	  This is counter-intuitive in a strict sense, but the callers
	  currently expect this.  They are calling the function
	  merely to determine whether VR->MIN <= VAL <= VR->MAX.  The
	  callers are applying the VR_RANGE/VR_ANTI_RANGE semantics
	  themselves.

	  This also applies to value_ranges_intersect_p and
	  range_includes_zero_p.  The semantics of VR_RANGE and
	  VR_ANTI_RANGE should be encoded here, but that also means
	  adapting the users of these functions to the new semantics.

   Benchmark compile/20001226-1.c compilation time after changing this
   function.  */

static inline int
value_inside_range (tree val, value_range_t * vr)
{
  int cmp1, cmp2;

  cmp1 = operand_less_p (val, vr->min);
  if (cmp1 == -2)
    return -2;
  if (cmp1 == 1)
    return 0;

  cmp2 = operand_less_p (vr->max, val);
  if (cmp2 == -2)
    return -2;

  return !cmp2;
}


/* Return true if value ranges VR0 and VR1 have a non-empty
   intersection.

   Benchmark compile/20001226-1.c compilation time after changing this
   function.
   */

static inline bool
value_ranges_intersect_p (value_range_t *vr0, value_range_t *vr1)
{
  /* The value ranges do not intersect if the maximum of the first range is
     less than the minimum of the second range or vice versa.
     When those relations are unknown, we can't do any better.  */
  if (operand_less_p (vr0->max, vr1->min) != 0)
    return false;
  if (operand_less_p (vr1->max, vr0->min) != 0)
    return false;
  return true;
}


/* Return true if VR includes the value zero, false otherwise.  FIXME,
   currently this will return false for an anti-range like ~[-4, 3].
   This will be wrong when the semantics of value_inside_range are
   modified (currently the users of this function expect these
   semantics).  */

static inline bool
range_includes_zero_p (value_range_t *vr)
{
  tree zero;

  gcc_assert (vr->type != VR_UNDEFINED
              && vr->type != VR_VARYING
	      && !symbolic_range_p (vr));

  zero = build_int_cst (TREE_TYPE (vr->min), 0);
  return (value_inside_range (zero, vr) == 1);
}

/* Return true if T, an SSA_NAME, is known to be nonnegative.  Return
   false otherwise or if no value range information is available.  */

bool
ssa_name_nonnegative_p (const_tree t)
{
  value_range_t *vr = get_value_range (t);

  if (INTEGRAL_TYPE_P (t)
      && TYPE_UNSIGNED (t))
    return true;

  if (!vr)
    return false;

  /* Testing for VR_ANTI_RANGE is not useful here as any anti-range
     which would return a useful value should be encoded as a VR_RANGE.  */
  if (vr->type == VR_RANGE)
    {
      int result = compare_values (vr->min, integer_zero_node);

      return (result == 0 || result == 1);
    }
  return false;
}

/* If OP has a value range with a single constant value return that,
   otherwise return NULL_TREE.  This returns OP itself if OP is a
   constant.  */

static tree
op_with_constant_singleton_value_range (tree op)
{
  value_range_t *vr;

  if (is_gimple_min_invariant (op))
    return op;

  if (TREE_CODE (op) != SSA_NAME)
    return NULL_TREE;

  vr = get_value_range (op);
  if (vr->type == VR_RANGE
      && operand_equal_p (vr->min, vr->max, 0)
      && is_gimple_min_invariant (vr->min))
    return vr->min;

  return NULL_TREE;
}


/* Extract value range information from an ASSERT_EXPR EXPR and store
   it in *VR_P.  */

static void
extract_range_from_assert (value_range_t *vr_p, tree expr)
{
  tree var, cond, limit, min, max, type;
  value_range_t *var_vr, *limit_vr;
  enum tree_code cond_code;

  var = ASSERT_EXPR_VAR (expr);
  cond = ASSERT_EXPR_COND (expr);

  gcc_assert (COMPARISON_CLASS_P (cond));

  /* Find VAR in the ASSERT_EXPR conditional.  */
  if (var == TREE_OPERAND (cond, 0)
      || TREE_CODE (TREE_OPERAND (cond, 0)) == PLUS_EXPR
      || TREE_CODE (TREE_OPERAND (cond, 0)) == NOP_EXPR)
    {
      /* If the predicate is of the form VAR COMP LIMIT, then we just
	 take LIMIT from the RHS and use the same comparison code.  */
      cond_code = TREE_CODE (cond);
      limit = TREE_OPERAND (cond, 1);
      cond = TREE_OPERAND (cond, 0);
    }
  else
    {
      /* If the predicate is of the form LIMIT COMP VAR, then we need
	 to flip around the comparison code to create the proper range
	 for VAR.  */
      cond_code = swap_tree_comparison (TREE_CODE (cond));
      limit = TREE_OPERAND (cond, 0);
      cond = TREE_OPERAND (cond, 1);
    }

  limit = avoid_overflow_infinity (limit);

  type = TREE_TYPE (var);
  gcc_assert (limit != var);

  /* For pointer arithmetic, we only keep track of pointer equality
     and inequality.  */
  if (POINTER_TYPE_P (type) && cond_code != NE_EXPR && cond_code != EQ_EXPR)
    {
      set_value_range_to_varying (vr_p);
      return;
    }

  /* If LIMIT is another SSA name and LIMIT has a range of its own,
     try to use LIMIT's range to avoid creating symbolic ranges
     unnecessarily. */
  limit_vr = (TREE_CODE (limit) == SSA_NAME) ? get_value_range (limit) : NULL;

  /* LIMIT's range is only interesting if it has any useful information.  */
  if (limit_vr
      && (limit_vr->type == VR_UNDEFINED
	  || limit_vr->type == VR_VARYING
	  || symbolic_range_p (limit_vr)))
    limit_vr = NULL;

  /* Initially, the new range has the same set of equivalences of
     VAR's range.  This will be revised before returning the final
     value.  Since assertions may be chained via mutually exclusive
     predicates, we will need to trim the set of equivalences before
     we are done.  */
  gcc_assert (vr_p->equiv == NULL);
  add_equivalence (&vr_p->equiv, var);

  /* Extract a new range based on the asserted comparison for VAR and
     LIMIT's value range.  Notice that if LIMIT has an anti-range, we
     will only use it for equality comparisons (EQ_EXPR).  For any
     other kind of assertion, we cannot derive a range from LIMIT's
     anti-range that can be used to describe the new range.  For
     instance, ASSERT_EXPR <x_2, x_2 <= b_4>.  If b_4 is ~[2, 10],
     then b_4 takes on the ranges [-INF, 1] and [11, +INF].  There is
     no single range for x_2 that could describe LE_EXPR, so we might
     as well build the range [b_4, +INF] for it.
     One special case we handle is extracting a range from a
     range test encoded as (unsigned)var + CST <= limit.  */
  if (TREE_CODE (cond) == NOP_EXPR
      || TREE_CODE (cond) == PLUS_EXPR)
    {
      if (TREE_CODE (cond) == PLUS_EXPR)
        {
          min = fold_build1 (NEGATE_EXPR, TREE_TYPE (TREE_OPERAND (cond, 1)),
			     TREE_OPERAND (cond, 1));
          max = int_const_binop (PLUS_EXPR, limit, min, 0);
	  cond = TREE_OPERAND (cond, 0);
	}
      else
	{
	  min = build_int_cst (TREE_TYPE (var), 0);
	  max = limit;
	}

      /* Make sure to not set TREE_OVERFLOW on the final type
	 conversion.  We are willingly interpreting large positive
	 unsigned values as negative singed values here.  */
      min = force_fit_type_double (TREE_TYPE (var), tree_to_double_int (min),
				   0, false);
      max = force_fit_type_double (TREE_TYPE (var), tree_to_double_int (max),
				   0, false);

      /* We can transform a max, min range to an anti-range or
         vice-versa.  Use set_and_canonicalize_value_range which does
	 this for us.  */
      if (cond_code == LE_EXPR)
        set_and_canonicalize_value_range (vr_p, VR_RANGE,
					  min, max, vr_p->equiv);
      else if (cond_code == GT_EXPR)
        set_and_canonicalize_value_range (vr_p, VR_ANTI_RANGE,
					  min, max, vr_p->equiv);
      else
	gcc_unreachable ();
    }
  else if (cond_code == EQ_EXPR)
    {
      enum value_range_type range_type;

      if (limit_vr)
	{
	  range_type = limit_vr->type;
	  min = limit_vr->min;
	  max = limit_vr->max;
	}
      else
	{
	  range_type = VR_RANGE;
	  min = limit;
	  max = limit;
	}

      set_value_range (vr_p, range_type, min, max, vr_p->equiv);

      /* When asserting the equality VAR == LIMIT and LIMIT is another
	 SSA name, the new range will also inherit the equivalence set
	 from LIMIT.  */
      if (TREE_CODE (limit) == SSA_NAME)
	add_equivalence (&vr_p->equiv, limit);
    }
  else if (cond_code == NE_EXPR)
    {
      /* As described above, when LIMIT's range is an anti-range and
	 this assertion is an inequality (NE_EXPR), then we cannot
	 derive anything from the anti-range.  For instance, if
	 LIMIT's range was ~[0, 0], the assertion 'VAR != LIMIT' does
	 not imply that VAR's range is [0, 0].  So, in the case of
	 anti-ranges, we just assert the inequality using LIMIT and
	 not its anti-range.

	 If LIMIT_VR is a range, we can only use it to build a new
	 anti-range if LIMIT_VR is a single-valued range.  For
	 instance, if LIMIT_VR is [0, 1], the predicate
	 VAR != [0, 1] does not mean that VAR's range is ~[0, 1].
	 Rather, it means that for value 0 VAR should be ~[0, 0]
	 and for value 1, VAR should be ~[1, 1].  We cannot
	 represent these ranges.

	 The only situation in which we can build a valid
	 anti-range is when LIMIT_VR is a single-valued range
	 (i.e., LIMIT_VR->MIN == LIMIT_VR->MAX).  In that case,
	 build the anti-range ~[LIMIT_VR->MIN, LIMIT_VR->MAX].  */
      if (limit_vr
	  && limit_vr->type == VR_RANGE
	  && compare_values (limit_vr->min, limit_vr->max) == 0)
	{
	  min = limit_vr->min;
	  max = limit_vr->max;
	}
      else
	{
	  /* In any other case, we cannot use LIMIT's range to build a
	     valid anti-range.  */
	  min = max = limit;
	}

      /* If MIN and MAX cover the whole range for their type, then
	 just use the original LIMIT.  */
      if (INTEGRAL_TYPE_P (type)
	  && vrp_val_is_min (min)
	  && vrp_val_is_max (max))
	min = max = limit;

      set_value_range (vr_p, VR_ANTI_RANGE, min, max, vr_p->equiv);
    }
  else if (cond_code == LE_EXPR || cond_code == LT_EXPR)
    {
      min = TYPE_MIN_VALUE (type);

      if (limit_vr == NULL || limit_vr->type == VR_ANTI_RANGE)
	max = limit;
      else
	{
	  /* If LIMIT_VR is of the form [N1, N2], we need to build the
	     range [MIN, N2] for LE_EXPR and [MIN, N2 - 1] for
	     LT_EXPR.  */
	  max = limit_vr->max;
	}

      /* If the maximum value forces us to be out of bounds, simply punt.
	 It would be pointless to try and do anything more since this
	 all should be optimized away above us.  */
      if ((cond_code == LT_EXPR
	   && compare_values (max, min) == 0)
	  || (CONSTANT_CLASS_P (max) && TREE_OVERFLOW (max)))
	set_value_range_to_varying (vr_p);
      else
	{
	  /* For LT_EXPR, we create the range [MIN, MAX - 1].  */
	  if (cond_code == LT_EXPR)
	    {
	      tree one = build_int_cst (TREE_TYPE (max), 1);
	      max = fold_build2 (MINUS_EXPR, TREE_TYPE (max), max, one);
	      if (EXPR_P (max))
		TREE_NO_WARNING (max) = 1;
	    }

	  set_value_range (vr_p, VR_RANGE, min, max, vr_p->equiv);
	}
    }
  else if (cond_code == GE_EXPR || cond_code == GT_EXPR)
    {
      max = TYPE_MAX_VALUE (type);

      if (limit_vr == NULL || limit_vr->type == VR_ANTI_RANGE)
	min = limit;
      else
	{
	  /* If LIMIT_VR is of the form [N1, N2], we need to build the
	     range [N1, MAX] for GE_EXPR and [N1 + 1, MAX] for
	     GT_EXPR.  */
	  min = limit_vr->min;
	}

      /* If the minimum value forces us to be out of bounds, simply punt.
	 It would be pointless to try and do anything more since this
	 all should be optimized away above us.  */
      if ((cond_code == GT_EXPR
	   && compare_values (min, max) == 0)
	  || (CONSTANT_CLASS_P (min) && TREE_OVERFLOW (min)))
	set_value_range_to_varying (vr_p);
      else
	{
	  /* For GT_EXPR, we create the range [MIN + 1, MAX].  */
	  if (cond_code == GT_EXPR)
	    {
	      tree one = build_int_cst (TREE_TYPE (min), 1);
	      min = fold_build2 (PLUS_EXPR, TREE_TYPE (min), min, one);
	      if (EXPR_P (min))
		TREE_NO_WARNING (min) = 1;
	    }

	  set_value_range (vr_p, VR_RANGE, min, max, vr_p->equiv);
	}
    }
  else
    gcc_unreachable ();

  /* If VAR already had a known range, it may happen that the new
     range we have computed and VAR's range are not compatible.  For
     instance,

	if (p_5 == NULL)
	  p_6 = ASSERT_EXPR <p_5, p_5 == NULL>;
	  x_7 = p_6->fld;
	  p_8 = ASSERT_EXPR <p_6, p_6 != NULL>;

     While the above comes from a faulty program, it will cause an ICE
     later because p_8 and p_6 will have incompatible ranges and at
     the same time will be considered equivalent.  A similar situation
     would arise from

     	if (i_5 > 10)
	  i_6 = ASSERT_EXPR <i_5, i_5 > 10>;
	  if (i_5 < 5)
	    i_7 = ASSERT_EXPR <i_6, i_6 < 5>;

     Again i_6 and i_7 will have incompatible ranges.  It would be
     pointless to try and do anything with i_7's range because
     anything dominated by 'if (i_5 < 5)' will be optimized away.
     Note, due to the wa in which simulation proceeds, the statement
     i_7 = ASSERT_EXPR <...> we would never be visited because the
     conditional 'if (i_5 < 5)' always evaluates to false.  However,
     this extra check does not hurt and may protect against future
     changes to VRP that may get into a situation similar to the
     NULL pointer dereference example.

     Note that these compatibility tests are only needed when dealing
     with ranges or a mix of range and anti-range.  If VAR_VR and VR_P
     are both anti-ranges, they will always be compatible, because two
     anti-ranges will always have a non-empty intersection.  */

  var_vr = get_value_range (var);

  /* We may need to make adjustments when VR_P and VAR_VR are numeric
     ranges or anti-ranges.  */
  if (vr_p->type == VR_VARYING
      || vr_p->type == VR_UNDEFINED
      || var_vr->type == VR_VARYING
      || var_vr->type == VR_UNDEFINED
      || symbolic_range_p (vr_p)
      || symbolic_range_p (var_vr))
    return;

  if (var_vr->type == VR_RANGE && vr_p->type == VR_RANGE)
    {
      /* If the two ranges have a non-empty intersection, we can
	 refine the resulting range.  Since the assert expression
	 creates an equivalency and at the same time it asserts a
	 predicate, we can take the intersection of the two ranges to
	 get better precision.  */
      if (value_ranges_intersect_p (var_vr, vr_p))
	{
	  /* Use the larger of the two minimums.  */
	  if (compare_values (vr_p->min, var_vr->min) == -1)
	    min = var_vr->min;
	  else
	    min = vr_p->min;

	  /* Use the smaller of the two maximums.  */
	  if (compare_values (vr_p->max, var_vr->max) == 1)
	    max = var_vr->max;
	  else
	    max = vr_p->max;

	  set_value_range (vr_p, vr_p->type, min, max, vr_p->equiv);
	}
      else
	{
	  /* The two ranges do not intersect, set the new range to
	     VARYING, because we will not be able to do anything
	     meaningful with it.  */
	  set_value_range_to_varying (vr_p);
	}
    }
  else if ((var_vr->type == VR_RANGE && vr_p->type == VR_ANTI_RANGE)
           || (var_vr->type == VR_ANTI_RANGE && vr_p->type == VR_RANGE))
    {
      /* A range and an anti-range will cancel each other only if
	 their ends are the same.  For instance, in the example above,
	 p_8's range ~[0, 0] and p_6's range [0, 0] are incompatible,
	 so VR_P should be set to VR_VARYING.  */
      if (compare_values (var_vr->min, vr_p->min) == 0
	  && compare_values (var_vr->max, vr_p->max) == 0)
	set_value_range_to_varying (vr_p);
      else
	{
	  tree min, max, anti_min, anti_max, real_min, real_max;
	  int cmp;

	  /* We want to compute the logical AND of the two ranges;
	     there are three cases to consider.


	     1. The VR_ANTI_RANGE range is completely within the
		VR_RANGE and the endpoints of the ranges are
		different.  In that case the resulting range
		should be whichever range is more precise.
		Typically that will be the VR_RANGE.

	     2. The VR_ANTI_RANGE is completely disjoint from
		the VR_RANGE.  In this case the resulting range
		should be the VR_RANGE.

	     3. There is some overlap between the VR_ANTI_RANGE
		and the VR_RANGE.

		3a. If the high limit of the VR_ANTI_RANGE resides
		    within the VR_RANGE, then the result is a new
		    VR_RANGE starting at the high limit of the
		    VR_ANTI_RANGE + 1 and extending to the
		    high limit of the original VR_RANGE.

		3b. If the low limit of the VR_ANTI_RANGE resides
		    within the VR_RANGE, then the result is a new
		    VR_RANGE starting at the low limit of the original
		    VR_RANGE and extending to the low limit of the
		    VR_ANTI_RANGE - 1.  */
	  if (vr_p->type == VR_ANTI_RANGE)
	    {
	      anti_min = vr_p->min;
	      anti_max = vr_p->max;
	      real_min = var_vr->min;
	      real_max = var_vr->max;
	    }
	  else
	    {
	      anti_min = var_vr->min;
	      anti_max = var_vr->max;
	      real_min = vr_p->min;
	      real_max = vr_p->max;
	    }


	  /* Case 1, VR_ANTI_RANGE completely within VR_RANGE,
	     not including any endpoints.  */
	  if (compare_values (anti_max, real_max) == -1
	      && compare_values (anti_min, real_min) == 1)
	    {
	      /* If the range is covering the whole valid range of
		 the type keep the anti-range.  */
	      if (!vrp_val_is_min (real_min)
		  || !vrp_val_is_max (real_max))
	        set_value_range (vr_p, VR_RANGE, real_min,
				 real_max, vr_p->equiv);
	    }
	  /* Case 2, VR_ANTI_RANGE completely disjoint from
	     VR_RANGE.  */
	  else if (compare_values (anti_min, real_max) == 1
		   || compare_values (anti_max, real_min) == -1)
	    {
	      set_value_range (vr_p, VR_RANGE, real_min,
			       real_max, vr_p->equiv);
	    }
	  /* Case 3a, the anti-range extends into the low
	     part of the real range.  Thus creating a new
	     low for the real range.  */
	  else if (((cmp = compare_values (anti_max, real_min)) == 1
		    || cmp == 0)
		   && compare_values (anti_max, real_max) == -1)
	    {
	      gcc_assert (!is_positive_overflow_infinity (anti_max));
	      if (needs_overflow_infinity (TREE_TYPE (anti_max))
		  && vrp_val_is_max (anti_max))
		{
		  if (!supports_overflow_infinity (TREE_TYPE (var_vr->min)))
		    {
		      set_value_range_to_varying (vr_p);
		      return;
		    }
		  min = positive_overflow_infinity (TREE_TYPE (var_vr->min));
		}
	      else if (!POINTER_TYPE_P (TREE_TYPE (var_vr->min)))
		min = fold_build2 (PLUS_EXPR, TREE_TYPE (var_vr->min),
				   anti_max,
				   build_int_cst (TREE_TYPE (var_vr->min), 1));
	      else
		min = fold_build2 (POINTER_PLUS_EXPR, TREE_TYPE (var_vr->min),
				   anti_max, size_int (1));
	      max = real_max;
	      set_value_range (vr_p, VR_RANGE, min, max, vr_p->equiv);
	    }
	  /* Case 3b, the anti-range extends into the high
	     part of the real range.  Thus creating a new
	     higher for the real range.  */
	  else if (compare_values (anti_min, real_min) == 1
		   && ((cmp = compare_values (anti_min, real_max)) == -1
		       || cmp == 0))
	    {
	      gcc_assert (!is_negative_overflow_infinity (anti_min));
	      if (needs_overflow_infinity (TREE_TYPE (anti_min))
		  && vrp_val_is_min (anti_min))
		{
		  if (!supports_overflow_infinity (TREE_TYPE (var_vr->min)))
		    {
		      set_value_range_to_varying (vr_p);
		      return;
		    }
		  max = negative_overflow_infinity (TREE_TYPE (var_vr->min));
		}
	      else if (!POINTER_TYPE_P (TREE_TYPE (var_vr->min)))
		max = fold_build2 (MINUS_EXPR, TREE_TYPE (var_vr->min),
				   anti_min,
				   build_int_cst (TREE_TYPE (var_vr->min), 1));
	      else
		max = fold_build2 (POINTER_PLUS_EXPR, TREE_TYPE (var_vr->min),
				   anti_min,
				   size_int (-1));
	      min = real_min;
	      set_value_range (vr_p, VR_RANGE, min, max, vr_p->equiv);
	    }
	}
    }
}


/* Extract range information from SSA name VAR and store it in VR.  If
   VAR has an interesting range, use it.  Otherwise, create the
   range [VAR, VAR] and return it.  This is useful in situations where
   we may have conditionals testing values of VARYING names.  For
   instance,

   	x_3 = y_5;
	if (x_3 > y_5)
	  ...

    Even if y_5 is deemed VARYING, we can determine that x_3 > y_5 is
    always false.  */

static void
extract_range_from_ssa_name (value_range_t *vr, tree var)
{
  value_range_t *var_vr = get_value_range (var);

  if (var_vr->type != VR_UNDEFINED && var_vr->type != VR_VARYING)
    copy_value_range (vr, var_vr);
  else
    set_value_range (vr, VR_RANGE, var, var, NULL);

  add_equivalence (&vr->equiv, var);
}


/* Wrapper around int_const_binop.  If the operation overflows and we
   are not using wrapping arithmetic, then adjust the result to be
   -INF or +INF depending on CODE, VAL1 and VAL2.  This can return
   NULL_TREE if we need to use an overflow infinity representation but
   the type does not support it.  */

static tree
vrp_int_const_binop (enum tree_code code, tree val1, tree val2)
{
  tree res;

  res = int_const_binop (code, val1, val2, 0);

  /* If we are using unsigned arithmetic, operate symbolically
     on -INF and +INF as int_const_binop only handles signed overflow.  */
  if (TYPE_UNSIGNED (TREE_TYPE (val1)))
    {
      int checkz = compare_values (res, val1);
      bool overflow = false;

      /* Ensure that res = val1 [+*] val2 >= val1
         or that res = val1 - val2 <= val1.  */
      if ((code == PLUS_EXPR
	   && !(checkz == 1 || checkz == 0))
          || (code == MINUS_EXPR
	      && !(checkz == 0 || checkz == -1)))
	{
	  overflow = true;
	}
      /* Checking for multiplication overflow is done by dividing the
	 output of the multiplication by the first input of the
	 multiplication.  If the result of that division operation is
	 not equal to the second input of the multiplication, then the
	 multiplication overflowed.  */
      else if (code == MULT_EXPR && !integer_zerop (val1))
	{
	  tree tmp = int_const_binop (TRUNC_DIV_EXPR,
				      res,
				      val1, 0);
	  int check = compare_values (tmp, val2);

	  if (check != 0)
	    overflow = true;
	}

      if (overflow)
	{
	  res = copy_node (res);
	  TREE_OVERFLOW (res) = 1;
	}

    }
  else if (TYPE_OVERFLOW_WRAPS (TREE_TYPE (val1)))
    /* If the singed operation wraps then int_const_binop has done
       everything we want.  */
    ;
  else if ((TREE_OVERFLOW (res)
	    && !TREE_OVERFLOW (val1)
	    && !TREE_OVERFLOW (val2))
	   || is_overflow_infinity (val1)
	   || is_overflow_infinity (val2))
    {
      /* If the operation overflowed but neither VAL1 nor VAL2 are
	 overflown, return -INF or +INF depending on the operation
	 and the combination of signs of the operands.  */
      int sgn1 = tree_int_cst_sgn (val1);
      int sgn2 = tree_int_cst_sgn (val2);

      if (needs_overflow_infinity (TREE_TYPE (res))
	  && !supports_overflow_infinity (TREE_TYPE (res)))
	return NULL_TREE;

      /* We have to punt on adding infinities of different signs,
	 since we can't tell what the sign of the result should be.
	 Likewise for subtracting infinities of the same sign.  */
      if (((code == PLUS_EXPR && sgn1 != sgn2)
	   || (code == MINUS_EXPR && sgn1 == sgn2))
	  && is_overflow_infinity (val1)
	  && is_overflow_infinity (val2))
	return NULL_TREE;

      /* Don't try to handle division or shifting of infinities.  */
      if ((code == TRUNC_DIV_EXPR
	   || code == FLOOR_DIV_EXPR
	   || code == CEIL_DIV_EXPR
	   || code == EXACT_DIV_EXPR
	   || code == ROUND_DIV_EXPR
	   || code == RSHIFT_EXPR)
	  && (is_overflow_infinity (val1)
	      || is_overflow_infinity (val2)))
	return NULL_TREE;

      /* Notice that we only need to handle the restricted set of
	 operations handled by extract_range_from_binary_expr.
	 Among them, only multiplication, addition and subtraction
	 can yield overflow without overflown operands because we
	 are working with integral types only... except in the
	 case VAL1 = -INF and VAL2 = -1 which overflows to +INF
	 for division too.  */

      /* For multiplication, the sign of the overflow is given
	 by the comparison of the signs of the operands.  */
      if ((code == MULT_EXPR && sgn1 == sgn2)
          /* For addition, the operands must be of the same sign
	     to yield an overflow.  Its sign is therefore that
	     of one of the operands, for example the first.  For
	     infinite operands X + -INF is negative, not positive.  */
	  || (code == PLUS_EXPR
	      && (sgn1 >= 0
		  ? !is_negative_overflow_infinity (val2)
		  : is_positive_overflow_infinity (val2)))
	  /* For subtraction, non-infinite operands must be of
	     different signs to yield an overflow.  Its sign is
	     therefore that of the first operand or the opposite of
	     that of the second operand.  A first operand of 0 counts
	     as positive here, for the corner case 0 - (-INF), which
	     overflows, but must yield +INF.  For infinite operands 0
	     - INF is negative, not positive.  */
	  || (code == MINUS_EXPR
	      && (sgn1 >= 0
		  ? !is_positive_overflow_infinity (val2)
		  : is_negative_overflow_infinity (val2)))
	  /* We only get in here with positive shift count, so the
	     overflow direction is the same as the sign of val1.
	     Actually rshift does not overflow at all, but we only
	     handle the case of shifting overflowed -INF and +INF.  */
	  || (code == RSHIFT_EXPR
	      && sgn1 >= 0)
	  /* For division, the only case is -INF / -1 = +INF.  */
	  || code == TRUNC_DIV_EXPR
	  || code == FLOOR_DIV_EXPR
	  || code == CEIL_DIV_EXPR
	  || code == EXACT_DIV_EXPR
	  || code == ROUND_DIV_EXPR)
	return (needs_overflow_infinity (TREE_TYPE (res))
		? positive_overflow_infinity (TREE_TYPE (res))
		: TYPE_MAX_VALUE (TREE_TYPE (res)));
      else
	return (needs_overflow_infinity (TREE_TYPE (res))
		? negative_overflow_infinity (TREE_TYPE (res))
		: TYPE_MIN_VALUE (TREE_TYPE (res)));
    }

  return res;
}


/* For range VR compute two double_int bitmasks.  In *MAY_BE_NONZERO
   bitmask if some bit is unset, it means for all numbers in the range
   the bit is 0, otherwise it might be 0 or 1.  In *MUST_BE_NONZERO
   bitmask if some bit is set, it means for all numbers in the range
   the bit is 1, otherwise it might be 0 or 1.  */

static bool
zero_nonzero_bits_from_vr (value_range_t *vr, double_int *may_be_nonzero,
			   double_int *must_be_nonzero)
{
  if (range_int_cst_p (vr))
    {
      if (range_int_cst_singleton_p (vr))
	{
	  *may_be_nonzero = tree_to_double_int (vr->min);
	  *must_be_nonzero = *may_be_nonzero;
	  return true;
	}
      if (tree_int_cst_sgn (vr->min) >= 0)
	{
	  double_int dmin = tree_to_double_int (vr->min);
	  double_int dmax = tree_to_double_int (vr->max);
	  double_int xor_mask = double_int_xor (dmin, dmax);
	  *may_be_nonzero = double_int_ior (dmin, dmax);
	  *must_be_nonzero = double_int_and (dmin, dmax);
	  if (xor_mask.high != 0)
	    {
	      unsigned HOST_WIDE_INT mask
		= ((unsigned HOST_WIDE_INT) 1
		   << floor_log2 (xor_mask.high)) - 1;
	      may_be_nonzero->low = ALL_ONES;
	      may_be_nonzero->high |= mask;
	      must_be_nonzero->low = 0;
	      must_be_nonzero->high &= ~mask;
	    }
	  else if (xor_mask.low != 0)
	    {
	      unsigned HOST_WIDE_INT mask
		= ((unsigned HOST_WIDE_INT) 1
		   << floor_log2 (xor_mask.low)) - 1;
	      may_be_nonzero->low |= mask;
	      must_be_nonzero->low &= ~mask;
	    }
	  return true;
	}
    }
  may_be_nonzero->low = ALL_ONES;
  may_be_nonzero->high = ALL_ONES;
  must_be_nonzero->low = 0;
  must_be_nonzero->high = 0;
  return false;
}


/* Extract range information from a binary expression EXPR based on
   the ranges of each of its operands and the expression code.  */

static void
extract_range_from_binary_expr (value_range_t *vr,
				enum tree_code code,
				tree expr_type, tree op0, tree op1)
{
  enum value_range_type type;
  tree min, max;
  int cmp;
  value_range_t vr0 = { VR_UNDEFINED, NULL_TREE, NULL_TREE, NULL };
  value_range_t vr1 = { VR_UNDEFINED, NULL_TREE, NULL_TREE, NULL };

  /* Not all binary expressions can be applied to ranges in a
     meaningful way.  Handle only arithmetic operations.  */
  if (code != PLUS_EXPR
      && code != MINUS_EXPR
      && code != POINTER_PLUS_EXPR
      && code != MULT_EXPR
      && code != TRUNC_DIV_EXPR
      && code != FLOOR_DIV_EXPR
      && code != CEIL_DIV_EXPR
      && code != EXACT_DIV_EXPR
      && code != ROUND_DIV_EXPR
      && code != TRUNC_MOD_EXPR
      && code != RSHIFT_EXPR
      && code != MIN_EXPR
      && code != MAX_EXPR
      && code != BIT_AND_EXPR
      && code != BIT_IOR_EXPR
      && code != TRUTH_AND_EXPR
      && code != TRUTH_OR_EXPR)
    {
      /* We can still do constant propagation here.  */
      tree const_op0 = op_with_constant_singleton_value_range (op0);
      tree const_op1 = op_with_constant_singleton_value_range (op1);
      if (const_op0 || const_op1)
	{
	  tree tem = fold_binary (code, expr_type,
				  const_op0 ? const_op0 : op0,
				  const_op1 ? const_op1 : op1);
	  if (tem
	      && is_gimple_min_invariant (tem)
	      && !is_overflow_infinity (tem))
	    {
	      set_value_range (vr, VR_RANGE, tem, tem, NULL);
	      return;
	    }
	}
      set_value_range_to_varying (vr);
      return;
    }

  /* Get value ranges for each operand.  For constant operands, create
     a new value range with the operand to simplify processing.  */
  if (TREE_CODE (op0) == SSA_NAME)
    vr0 = *(get_value_range (op0));
  else if (is_gimple_min_invariant (op0))
    set_value_range_to_value (&vr0, op0, NULL);
  else
    set_value_range_to_varying (&vr0);

  if (TREE_CODE (op1) == SSA_NAME)
    vr1 = *(get_value_range (op1));
  else if (is_gimple_min_invariant (op1))
    set_value_range_to_value (&vr1, op1, NULL);
  else
    set_value_range_to_varying (&vr1);

  /* If either range is UNDEFINED, so is the result.  */
  if (vr0.type == VR_UNDEFINED || vr1.type == VR_UNDEFINED)
    {
      set_value_range_to_undefined (vr);
      return;
    }

  /* The type of the resulting value range defaults to VR0.TYPE.  */
  type = vr0.type;

  /* Refuse to operate on VARYING ranges, ranges of different kinds
     and symbolic ranges.  As an exception, we allow BIT_AND_EXPR
     because we may be able to derive a useful range even if one of
     the operands is VR_VARYING or symbolic range.  Similarly for
     divisions.  TODO, we may be able to derive anti-ranges in
     some cases.  */
  if (code != BIT_AND_EXPR
      && code != TRUTH_AND_EXPR
      && code != TRUTH_OR_EXPR
      && code != TRUNC_DIV_EXPR
      && code != FLOOR_DIV_EXPR
      && code != CEIL_DIV_EXPR
      && code != EXACT_DIV_EXPR
      && code != ROUND_DIV_EXPR
      && code != TRUNC_MOD_EXPR
      && (vr0.type == VR_VARYING
	  || vr1.type == VR_VARYING
	  || vr0.type != vr1.type
	  || symbolic_range_p (&vr0)
	  || symbolic_range_p (&vr1)))
    {
      set_value_range_to_varying (vr);
      return;
    }

  /* Now evaluate the expression to determine the new range.  */
  if (POINTER_TYPE_P (expr_type)
      || POINTER_TYPE_P (TREE_TYPE (op0))
      || POINTER_TYPE_P (TREE_TYPE (op1)))
    {
      if (code == MIN_EXPR || code == MAX_EXPR)
	{
	  /* For MIN/MAX expressions with pointers, we only care about
	     nullness, if both are non null, then the result is nonnull.
	     If both are null, then the result is null. Otherwise they
	     are varying.  */
	  if (range_is_nonnull (&vr0) && range_is_nonnull (&vr1))
	    set_value_range_to_nonnull (vr, expr_type);
	  else if (range_is_null (&vr0) && range_is_null (&vr1))
	    set_value_range_to_null (vr, expr_type);
	  else
	    set_value_range_to_varying (vr);

	  return;
	}
      if (code == POINTER_PLUS_EXPR)
	{
	  /* For pointer types, we are really only interested in asserting
	     whether the expression evaluates to non-NULL.  */
	  if (range_is_nonnull (&vr0) || range_is_nonnull (&vr1))
	    set_value_range_to_nonnull (vr, expr_type);
	  else if (range_is_null (&vr0) && range_is_null (&vr1))
	    set_value_range_to_null (vr, expr_type);
	  else
	    set_value_range_to_varying (vr);
	}
      else if (code == BIT_AND_EXPR)
	{
	  /* For pointer types, we are really only interested in asserting
	     whether the expression evaluates to non-NULL.  */
	  if (range_is_nonnull (&vr0) && range_is_nonnull (&vr1))
	    set_value_range_to_nonnull (vr, expr_type);
	  else if (range_is_null (&vr0) || range_is_null (&vr1))
	    set_value_range_to_null (vr, expr_type);
	  else
	    set_value_range_to_varying (vr);
	}
      else
	gcc_unreachable ();

      return;
    }

  /* For integer ranges, apply the operation to each end of the
     range and see what we end up with.  */
  if (code == TRUTH_AND_EXPR
      || code == TRUTH_OR_EXPR)
    {
      /* If one of the operands is zero, we know that the whole
	 expression evaluates zero.  */
      if (code == TRUTH_AND_EXPR
	  && ((vr0.type == VR_RANGE
	       && integer_zerop (vr0.min)
	       && integer_zerop (vr0.max))
	      || (vr1.type == VR_RANGE
		  && integer_zerop (vr1.min)
		  && integer_zerop (vr1.max))))
	{
	  type = VR_RANGE;
	  min = max = build_int_cst (expr_type, 0);
	}
      /* If one of the operands is one, we know that the whole
	 expression evaluates one.  */
      else if (code == TRUTH_OR_EXPR
	       && ((vr0.type == VR_RANGE
		    && integer_onep (vr0.min)
		    && integer_onep (vr0.max))
		   || (vr1.type == VR_RANGE
		       && integer_onep (vr1.min)
		       && integer_onep (vr1.max))))
	{
	  type = VR_RANGE;
	  min = max = build_int_cst (expr_type, 1);
	}
      else if (vr0.type != VR_VARYING
	       && vr1.type != VR_VARYING
	       && vr0.type == vr1.type
	       && !symbolic_range_p (&vr0)
	       && !overflow_infinity_range_p (&vr0)
	       && !symbolic_range_p (&vr1)
	       && !overflow_infinity_range_p (&vr1))
	{
	  /* Boolean expressions cannot be folded with int_const_binop.  */
	  min = fold_binary (code, expr_type, vr0.min, vr1.min);
	  max = fold_binary (code, expr_type, vr0.max, vr1.max);
	}
      else
	{
	  /* The result of a TRUTH_*_EXPR is always true or false.  */
	  set_value_range_to_truthvalue (vr, expr_type);
	  return;
	}
    }
  else if (code == PLUS_EXPR
	   || code == MIN_EXPR
	   || code == MAX_EXPR)
    {
      /* If we have a PLUS_EXPR with two VR_ANTI_RANGEs, drop to
	 VR_VARYING.  It would take more effort to compute a precise
	 range for such a case.  For example, if we have op0 == 1 and
	 op1 == -1 with their ranges both being ~[0,0], we would have
	 op0 + op1 == 0, so we cannot claim that the sum is in ~[0,0].
	 Note that we are guaranteed to have vr0.type == vr1.type at
	 this point.  */
      if (vr0.type == VR_ANTI_RANGE)
	{
	  if (code == PLUS_EXPR)
	    {
	      set_value_range_to_varying (vr);
	      return;
	    }
	  /* For MIN_EXPR and MAX_EXPR with two VR_ANTI_RANGEs,
	     the resulting VR_ANTI_RANGE is the same - intersection
	     of the two ranges.  */
	  min = vrp_int_const_binop (MAX_EXPR, vr0.min, vr1.min);
	  max = vrp_int_const_binop (MIN_EXPR, vr0.max, vr1.max);
	}
      else
	{
	  /* For operations that make the resulting range directly
	     proportional to the original ranges, apply the operation to
	     the same end of each range.  */
	  min = vrp_int_const_binop (code, vr0.min, vr1.min);
	  max = vrp_int_const_binop (code, vr0.max, vr1.max);
	}

      /* If both additions overflowed the range kind is still correct.
	 This happens regularly with subtracting something in unsigned
	 arithmetic.
         ???  See PR30318 for all the cases we do not handle.  */
      if (code == PLUS_EXPR
	  && (TREE_OVERFLOW (min) && !is_overflow_infinity (min))
	  && (TREE_OVERFLOW (max) && !is_overflow_infinity (max)))
	{
	  min = build_int_cst_wide (TREE_TYPE (min),
				    TREE_INT_CST_LOW (min),
				    TREE_INT_CST_HIGH (min));
	  max = build_int_cst_wide (TREE_TYPE (max),
				    TREE_INT_CST_LOW (max),
				    TREE_INT_CST_HIGH (max));
	}
    }
  else if (code == MULT_EXPR
	   || code == TRUNC_DIV_EXPR
	   || code == FLOOR_DIV_EXPR
	   || code == CEIL_DIV_EXPR
	   || code == EXACT_DIV_EXPR
	   || code == ROUND_DIV_EXPR
	   || code == RSHIFT_EXPR)
    {
      tree val[4];
      size_t i;
      bool sop;

      /* If we have an unsigned MULT_EXPR with two VR_ANTI_RANGEs,
	 drop to VR_VARYING.  It would take more effort to compute a
	 precise range for such a case.  For example, if we have
	 op0 == 65536 and op1 == 65536 with their ranges both being
	 ~[0,0] on a 32-bit machine, we would have op0 * op1 == 0, so
	 we cannot claim that the product is in ~[0,0].  Note that we
	 are guaranteed to have vr0.type == vr1.type at this
	 point.  */
      if (code == MULT_EXPR
	  && vr0.type == VR_ANTI_RANGE
	  && !TYPE_OVERFLOW_UNDEFINED (TREE_TYPE (op0)))
	{
	  set_value_range_to_varying (vr);
	  return;
	}

      /* If we have a RSHIFT_EXPR with any shift values outside [0..prec-1],
	 then drop to VR_VARYING.  Outside of this range we get undefined
	 behavior from the shift operation.  We cannot even trust
	 SHIFT_COUNT_TRUNCATED at this stage, because that applies to rtl
	 shifts, and the operation at the tree level may be widened.  */
      if (code == RSHIFT_EXPR)
	{
	  if (vr1.type == VR_ANTI_RANGE
	      || !vrp_expr_computes_nonnegative (op1, &sop)
	      || (operand_less_p
		  (build_int_cst (TREE_TYPE (vr1.max),
				  TYPE_PRECISION (expr_type) - 1),
		   vr1.max) != 0))
	    {
	      set_value_range_to_varying (vr);
	      return;
	    }
	}

      else if ((code == TRUNC_DIV_EXPR
		|| code == FLOOR_DIV_EXPR
		|| code == CEIL_DIV_EXPR
		|| code == EXACT_DIV_EXPR
		|| code == ROUND_DIV_EXPR)
	       && (vr0.type != VR_RANGE || symbolic_range_p (&vr0)))
	{
	  /* For division, if op1 has VR_RANGE but op0 does not, something
	     can be deduced just from that range.  Say [min, max] / [4, max]
	     gives [min / 4, max / 4] range.  */
	  if (vr1.type == VR_RANGE
	      && !symbolic_range_p (&vr1)
	      && !range_includes_zero_p (&vr1))
	    {
	      vr0.type = type = VR_RANGE;
	      vr0.min = vrp_val_min (TREE_TYPE (op0));
	      vr0.max = vrp_val_max (TREE_TYPE (op1));
	    }
	  else
	    {
	      set_value_range_to_varying (vr);
	      return;
	    }
	}

      /* For divisions, if flag_non_call_exceptions is true, we must
	 not eliminate a division by zero.  */
      if ((code == TRUNC_DIV_EXPR
	   || code == FLOOR_DIV_EXPR
	   || code == CEIL_DIV_EXPR
	   || code == EXACT_DIV_EXPR
	   || code == ROUND_DIV_EXPR)
	  && cfun->can_throw_non_call_exceptions
	  && (vr1.type != VR_RANGE
	      || symbolic_range_p (&vr1)
	      || range_includes_zero_p (&vr1)))
	{
	  set_value_range_to_varying (vr);
	  return;
	}

      /* For divisions, if op0 is VR_RANGE, we can deduce a range
	 even if op1 is VR_VARYING, VR_ANTI_RANGE, symbolic or can
	 include 0.  */
      if ((code == TRUNC_DIV_EXPR
	   || code == FLOOR_DIV_EXPR
	   || code == CEIL_DIV_EXPR
	   || code == EXACT_DIV_EXPR
	   || code == ROUND_DIV_EXPR)
	  && vr0.type == VR_RANGE
	  && (vr1.type != VR_RANGE
	      || symbolic_range_p (&vr1)
	      || range_includes_zero_p (&vr1)))
	{
	  tree zero = build_int_cst (TREE_TYPE (vr0.min), 0);
	  int cmp;

	  sop = false;
	  min = NULL_TREE;
	  max = NULL_TREE;
	  if (vrp_expr_computes_nonnegative (op1, &sop) && !sop)
	    {
	      /* For unsigned division or when divisor is known
		 to be non-negative, the range has to cover
		 all numbers from 0 to max for positive max
		 and all numbers from min to 0 for negative min.  */
	      cmp = compare_values (vr0.max, zero);
	      if (cmp == -1)
		max = zero;
	      else if (cmp == 0 || cmp == 1)
		max = vr0.max;
	      else
		type = VR_VARYING;
	      cmp = compare_values (vr0.min, zero);
	      if (cmp == 1)
		min = zero;
	      else if (cmp == 0 || cmp == -1)
		min = vr0.min;
	      else
		type = VR_VARYING;
	    }
	  else
	    {
	      /* Otherwise the range is -max .. max or min .. -min
		 depending on which bound is bigger in absolute value,
		 as the division can change the sign.  */
	      abs_extent_range (vr, vr0.min, vr0.max);
	      return;
	    }
	  if (type == VR_VARYING)
	    {
	      set_value_range_to_varying (vr);
	      return;
	    }
	}

      /* Multiplications and divisions are a bit tricky to handle,
	 depending on the mix of signs we have in the two ranges, we
	 need to operate on different values to get the minimum and
	 maximum values for the new range.  One approach is to figure
	 out all the variations of range combinations and do the
	 operations.

	 However, this involves several calls to compare_values and it
	 is pretty convoluted.  It's simpler to do the 4 operations
	 (MIN0 OP MIN1, MIN0 OP MAX1, MAX0 OP MIN1 and MAX0 OP MAX0 OP
	 MAX1) and then figure the smallest and largest values to form
	 the new range.  */
      else
	{
	  gcc_assert ((vr0.type == VR_RANGE
		       || (code == MULT_EXPR && vr0.type == VR_ANTI_RANGE))
		      && vr0.type == vr1.type);

	  /* Compute the 4 cross operations.  */
	  sop = false;
	  val[0] = vrp_int_const_binop (code, vr0.min, vr1.min);
	  if (val[0] == NULL_TREE)
	    sop = true;

	  if (vr1.max == vr1.min)
	    val[1] = NULL_TREE;
	  else
	    {
	      val[1] = vrp_int_const_binop (code, vr0.min, vr1.max);
	      if (val[1] == NULL_TREE)
		sop = true;
	    }

	  if (vr0.max == vr0.min)
	    val[2] = NULL_TREE;
	  else
	    {
	      val[2] = vrp_int_const_binop (code, vr0.max, vr1.min);
	      if (val[2] == NULL_TREE)
		sop = true;
	    }

	  if (vr0.min == vr0.max || vr1.min == vr1.max)
	    val[3] = NULL_TREE;
	  else
	    {
	      val[3] = vrp_int_const_binop (code, vr0.max, vr1.max);
	      if (val[3] == NULL_TREE)
		sop = true;
	    }

	  if (sop)
	    {
	      set_value_range_to_varying (vr);
	      return;
	    }

	  /* Set MIN to the minimum of VAL[i] and MAX to the maximum
	     of VAL[i].  */
	  min = val[0];
	  max = val[0];
	  for (i = 1; i < 4; i++)
	    {
	      if (!is_gimple_min_invariant (min)
		  || (TREE_OVERFLOW (min) && !is_overflow_infinity (min))
		  || !is_gimple_min_invariant (max)
		  || (TREE_OVERFLOW (max) && !is_overflow_infinity (max)))
		break;

	      if (val[i])
		{
		  if (!is_gimple_min_invariant (val[i])
		      || (TREE_OVERFLOW (val[i])
			  && !is_overflow_infinity (val[i])))
		    {
		      /* If we found an overflowed value, set MIN and MAX
			 to it so that we set the resulting range to
			 VARYING.  */
		      min = max = val[i];
		      break;
		    }

		  if (compare_values (val[i], min) == -1)
		    min = val[i];

		  if (compare_values (val[i], max) == 1)
		    max = val[i];
		}
	    }
	}
    }
  else if (code == TRUNC_MOD_EXPR)
    {
      bool sop = false;
      if (vr1.type != VR_RANGE
	  || symbolic_range_p (&vr1)
	  || range_includes_zero_p (&vr1)
	  || vrp_val_is_min (vr1.min))
	{
	  set_value_range_to_varying (vr);
	  return;
	}
      type = VR_RANGE;
      /* Compute MAX <|vr1.min|, |vr1.max|> - 1.  */
      max = fold_unary_to_constant (ABS_EXPR, TREE_TYPE (vr1.min), vr1.min);
      if (tree_int_cst_lt (max, vr1.max))
	max = vr1.max;
      max = int_const_binop (MINUS_EXPR, max, integer_one_node, 0);
      /* If the dividend is non-negative the modulus will be
	 non-negative as well.  */
      if (TYPE_UNSIGNED (TREE_TYPE (max))
	  || (vrp_expr_computes_nonnegative (op0, &sop) && !sop))
	min = build_int_cst (TREE_TYPE (max), 0);
      else
	min = fold_unary_to_constant (NEGATE_EXPR, TREE_TYPE (max), max);
    }
  else if (code == MINUS_EXPR)
    {
      /* If we have a MINUS_EXPR with two VR_ANTI_RANGEs, drop to
	 VR_VARYING.  It would take more effort to compute a precise
	 range for such a case.  For example, if we have op0 == 1 and
	 op1 == 1 with their ranges both being ~[0,0], we would have
	 op0 - op1 == 0, so we cannot claim that the difference is in
	 ~[0,0].  Note that we are guaranteed to have
	 vr0.type == vr1.type at this point.  */
      if (vr0.type == VR_ANTI_RANGE)
	{
	  set_value_range_to_varying (vr);
	  return;
	}

      /* For MINUS_EXPR, apply the operation to the opposite ends of
	 each range.  */
      min = vrp_int_const_binop (code, vr0.min, vr1.max);
      max = vrp_int_const_binop (code, vr0.max, vr1.min);
    }
  else if (code == BIT_AND_EXPR || code == BIT_IOR_EXPR)
    {
      bool vr0_int_cst_singleton_p, vr1_int_cst_singleton_p;
      bool int_cst_range0, int_cst_range1;
      double_int may_be_nonzero0, may_be_nonzero1;
      double_int must_be_nonzero0, must_be_nonzero1;

      vr0_int_cst_singleton_p = range_int_cst_singleton_p (&vr0);
      vr1_int_cst_singleton_p = range_int_cst_singleton_p (&vr1);
      int_cst_range0 = zero_nonzero_bits_from_vr (&vr0, &may_be_nonzero0,
						  &must_be_nonzero0);
      int_cst_range1 = zero_nonzero_bits_from_vr (&vr1, &may_be_nonzero1,
						  &must_be_nonzero1);

      type = VR_RANGE;
      if (vr0_int_cst_singleton_p && vr1_int_cst_singleton_p)
	min = max = int_const_binop (code, vr0.max, vr1.max, 0);
      else if (!int_cst_range0 && !int_cst_range1)
	{
	  set_value_range_to_varying (vr);
	  return;
	}
      else if (code == BIT_AND_EXPR)
	{
	  min = double_int_to_tree (expr_type,
				    double_int_and (must_be_nonzero0,
						    must_be_nonzero1));
	  max = double_int_to_tree (expr_type,
				    double_int_and (may_be_nonzero0,
						    may_be_nonzero1));
	  if (TREE_OVERFLOW (min) || tree_int_cst_sgn (min) < 0)
	    min = NULL_TREE;
	  if (TREE_OVERFLOW (max) || tree_int_cst_sgn (max) < 0)
	    max = NULL_TREE;
	  if (int_cst_range0 && tree_int_cst_sgn (vr0.min) >= 0)
	    {
	      if (min == NULL_TREE)
		min = build_int_cst (expr_type, 0);
	      if (max == NULL_TREE || tree_int_cst_lt (vr0.max, max))
		max = vr0.max;
	    }
	  if (int_cst_range1 && tree_int_cst_sgn (vr1.min) >= 0)
	    {
	      if (min == NULL_TREE)
		min = build_int_cst (expr_type, 0);
	      if (max == NULL_TREE || tree_int_cst_lt (vr1.max, max))
		max = vr1.max;
	    }
	}
      else if (!int_cst_range0
	       || !int_cst_range1
	       || tree_int_cst_sgn (vr0.min) < 0
	       || tree_int_cst_sgn (vr1.min) < 0)
	{
	  set_value_range_to_varying (vr);
	  return;
	}
      else
	{
	  min = double_int_to_tree (expr_type,
				    double_int_ior (must_be_nonzero0,
						    must_be_nonzero1));
	  max = double_int_to_tree (expr_type,
				    double_int_ior (may_be_nonzero0,
						    may_be_nonzero1));
	  if (TREE_OVERFLOW (min) || tree_int_cst_sgn (min) < 0)
	    min = vr0.min;
	  else
	    min = vrp_int_const_binop (MAX_EXPR, min, vr0.min);
	  if (TREE_OVERFLOW (max) || tree_int_cst_sgn (max) < 0)
	    max = NULL_TREE;
	  min = vrp_int_const_binop (MAX_EXPR, min, vr1.min);
	}
    }
  else
    gcc_unreachable ();

  /* If either MIN or MAX overflowed, then set the resulting range to
     VARYING.  But we do accept an overflow infinity
     representation.  */
  if (min == NULL_TREE
      || !is_gimple_min_invariant (min)
      || (TREE_OVERFLOW (min) && !is_overflow_infinity (min))
      || max == NULL_TREE
      || !is_gimple_min_invariant (max)
      || (TREE_OVERFLOW (max) && !is_overflow_infinity (max)))
    {
      set_value_range_to_varying (vr);
      return;
    }

  /* We punt if:
     1) [-INF, +INF]
     2) [-INF, +-INF(OVF)]
     3) [+-INF(OVF), +INF]
     4) [+-INF(OVF), +-INF(OVF)]
     We learn nothing when we have INF and INF(OVF) on both sides.
     Note that we do accept [-INF, -INF] and [+INF, +INF] without
     overflow.  */
  if ((vrp_val_is_min (min) || is_overflow_infinity (min))
      && (vrp_val_is_max (max) || is_overflow_infinity (max)))
    {
      set_value_range_to_varying (vr);
      return;
    }

  cmp = compare_values (min, max);
  if (cmp == -2 || cmp == 1)
    {
      /* If the new range has its limits swapped around (MIN > MAX),
	 then the operation caused one of them to wrap around, mark
	 the new range VARYING.  */
      set_value_range_to_varying (vr);
    }
  else
    set_value_range (vr, type, min, max, NULL);
}


/* Extract range information from a unary expression EXPR based on
   the range of its operand and the expression code.  */

static void
extract_range_from_unary_expr (value_range_t *vr, enum tree_code code,
			       tree type, tree op0)
{
  tree min, max;
  int cmp;
  value_range_t vr0 = { VR_UNDEFINED, NULL_TREE, NULL_TREE, NULL };

  /* Refuse to operate on certain unary expressions for which we
     cannot easily determine a resulting range.  */
  if (code == FIX_TRUNC_EXPR
      || code == FLOAT_EXPR
      || code == BIT_NOT_EXPR
      || code == CONJ_EXPR)
    {
      /* We can still do constant propagation here.  */
      if ((op0 = op_with_constant_singleton_value_range (op0)) != NULL_TREE)
	{
	  tree tem = fold_unary (code, type, op0);
	  if (tem
	      && is_gimple_min_invariant (tem)
	      && !is_overflow_infinity (tem))
	    {
	      set_value_range (vr, VR_RANGE, tem, tem, NULL);
	      return;
	    }
	}
      set_value_range_to_varying (vr);
      return;
    }

  /* Get value ranges for the operand.  For constant operands, create
     a new value range with the operand to simplify processing.  */
  if (TREE_CODE (op0) == SSA_NAME)
    vr0 = *(get_value_range (op0));
  else if (is_gimple_min_invariant (op0))
    set_value_range_to_value (&vr0, op0, NULL);
  else
    set_value_range_to_varying (&vr0);

  /* If VR0 is UNDEFINED, so is the result.  */
  if (vr0.type == VR_UNDEFINED)
    {
      set_value_range_to_undefined (vr);
      return;
    }

  /* Refuse to operate on symbolic ranges, or if neither operand is
     a pointer or integral type.  */
  if ((!INTEGRAL_TYPE_P (TREE_TYPE (op0))
       && !POINTER_TYPE_P (TREE_TYPE (op0)))
      || (vr0.type != VR_VARYING
	  && symbolic_range_p (&vr0)))
    {
      set_value_range_to_varying (vr);
      return;
    }

  /* If the expression involves pointers, we are only interested in
     determining if it evaluates to NULL [0, 0] or non-NULL (~[0, 0]).  */
  if (POINTER_TYPE_P (type) || POINTER_TYPE_P (TREE_TYPE (op0)))
    {
      bool sop;

      sop = false;
      if (range_is_nonnull (&vr0)
	  || (tree_unary_nonzero_warnv_p (code, type, op0, &sop)
	      && !sop))
	set_value_range_to_nonnull (vr, type);
      else if (range_is_null (&vr0))
	set_value_range_to_null (vr, type);
      else
	set_value_range_to_varying (vr);

      return;
    }

  /* Handle unary expressions on integer ranges.  */
  if (CONVERT_EXPR_CODE_P (code)
      && INTEGRAL_TYPE_P (type)
      && INTEGRAL_TYPE_P (TREE_TYPE (op0)))
    {
      tree inner_type = TREE_TYPE (op0);
      tree outer_type = type;

      /* If VR0 is varying and we increase the type precision, assume
	 a full range for the following transformation.  */
      if (vr0.type == VR_VARYING
	  && TYPE_PRECISION (inner_type) < TYPE_PRECISION (outer_type))
	{
	  vr0.type = VR_RANGE;
	  vr0.min = TYPE_MIN_VALUE (inner_type);
	  vr0.max = TYPE_MAX_VALUE (inner_type);
	}

      /* If VR0 is a constant range or anti-range and the conversion is
	 not truncating we can convert the min and max values and
	 canonicalize the resulting range.  Otherwise we can do the
	 conversion if the size of the range is less than what the
	 precision of the target type can represent and the range is
	 not an anti-range.  */
      if ((vr0.type == VR_RANGE
	   || vr0.type == VR_ANTI_RANGE)
	  && TREE_CODE (vr0.min) == INTEGER_CST
	  && TREE_CODE (vr0.max) == INTEGER_CST
	  && (!is_overflow_infinity (vr0.min)
	      || (vr0.type == VR_RANGE
		  && TYPE_PRECISION (outer_type) > TYPE_PRECISION (inner_type)
		  && needs_overflow_infinity (outer_type)
		  && supports_overflow_infinity (outer_type)))
	  && (!is_overflow_infinity (vr0.max)
	      || (vr0.type == VR_RANGE
		  && TYPE_PRECISION (outer_type) > TYPE_PRECISION (inner_type)
		  && needs_overflow_infinity (outer_type)
		  && supports_overflow_infinity (outer_type)))
	  && (TYPE_PRECISION (outer_type) >= TYPE_PRECISION (inner_type)
	      || (vr0.type == VR_RANGE
		  && integer_zerop (int_const_binop (RSHIFT_EXPR,
		       int_const_binop (MINUS_EXPR, vr0.max, vr0.min, 0),
		         size_int (TYPE_PRECISION (outer_type)), 0)))))
	{
	  tree new_min, new_max;
	  new_min = force_fit_type_double (outer_type,
					   tree_to_double_int (vr0.min),
					   0, false);
	  new_max = force_fit_type_double (outer_type,
					   tree_to_double_int (vr0.max),
					   0, false);
	  if (is_overflow_infinity (vr0.min))
	    new_min = negative_overflow_infinity (outer_type);
	  if (is_overflow_infinity (vr0.max))
	    new_max = positive_overflow_infinity (outer_type);
	  set_and_canonicalize_value_range (vr, vr0.type,
					    new_min, new_max, NULL);
	  return;
	}

      set_value_range_to_varying (vr);
      return;
    }

  /* Conversion of a VR_VARYING value to a wider type can result
     in a usable range.  So wait until after we've handled conversions
     before dropping the result to VR_VARYING if we had a source
     operand that is VR_VARYING.  */
  if (vr0.type == VR_VARYING)
    {
      set_value_range_to_varying (vr);
      return;
    }

  /* Apply the operation to each end of the range and see what we end
     up with.  */
  if (code == NEGATE_EXPR
      && !TYPE_UNSIGNED (type))
    {
      /* NEGATE_EXPR flips the range around.  We need to treat
	 TYPE_MIN_VALUE specially.  */
      if (is_positive_overflow_infinity (vr0.max))
	min = negative_overflow_infinity (type);
      else if (is_negative_overflow_infinity (vr0.max))
	min = positive_overflow_infinity (type);
      else if (!vrp_val_is_min (vr0.max))
	min = fold_unary_to_constant (code, type, vr0.max);
      else if (needs_overflow_infinity (type))
	{
	  if (supports_overflow_infinity (type)
	      && !is_overflow_infinity (vr0.min)
	      && !vrp_val_is_min (vr0.min))
	    min = positive_overflow_infinity (type);
	  else
	    {
	      set_value_range_to_varying (vr);
	      return;
	    }
	}
      else
	min = TYPE_MIN_VALUE (type);

      if (is_positive_overflow_infinity (vr0.min))
	max = negative_overflow_infinity (type);
      else if (is_negative_overflow_infinity (vr0.min))
	max = positive_overflow_infinity (type);
      else if (!vrp_val_is_min (vr0.min))
	max = fold_unary_to_constant (code, type, vr0.min);
      else if (needs_overflow_infinity (type))
	{
	  if (supports_overflow_infinity (type))
	    max = positive_overflow_infinity (type);
	  else
	    {
	      set_value_range_to_varying (vr);
	      return;
	    }
	}
      else
	max = TYPE_MIN_VALUE (type);
    }
  else if (code == NEGATE_EXPR
	   && TYPE_UNSIGNED (type))
    {
      if (!range_includes_zero_p (&vr0))
	{
	  max = fold_unary_to_constant (code, type, vr0.min);
	  min = fold_unary_to_constant (code, type, vr0.max);
	}
      else
	{
	  if (range_is_null (&vr0))
	    set_value_range_to_null (vr, type);
	  else
	    set_value_range_to_varying (vr);
	  return;
	}
    }
  else if (code == ABS_EXPR
           && !TYPE_UNSIGNED (type))
    {
      /* -TYPE_MIN_VALUE = TYPE_MIN_VALUE with flag_wrapv so we can't get a
         useful range.  */
      if (!TYPE_OVERFLOW_UNDEFINED (type)
	  && ((vr0.type == VR_RANGE
	       && vrp_val_is_min (vr0.min))
	      || (vr0.type == VR_ANTI_RANGE
		  && !vrp_val_is_min (vr0.min)
		  && !range_includes_zero_p (&vr0))))
	{
	  set_value_range_to_varying (vr);
	  return;
	}

      /* ABS_EXPR may flip the range around, if the original range
	 included negative values.  */
      if (is_overflow_infinity (vr0.min))
	min = positive_overflow_infinity (type);
      else if (!vrp_val_is_min (vr0.min))
	min = fold_unary_to_constant (code, type, vr0.min);
      else if (!needs_overflow_infinity (type))
	min = TYPE_MAX_VALUE (type);
      else if (supports_overflow_infinity (type))
	min = positive_overflow_infinity (type);
      else
	{
	  set_value_range_to_varying (vr);
	  return;
	}

      if (is_overflow_infinity (vr0.max))
	max = positive_overflow_infinity (type);
      else if (!vrp_val_is_min (vr0.max))
	max = fold_unary_to_constant (code, type, vr0.max);
      else if (!needs_overflow_infinity (type))
	max = TYPE_MAX_VALUE (type);
      else if (supports_overflow_infinity (type)
	       /* We shouldn't generate [+INF, +INF] as set_value_range
		  doesn't like this and ICEs.  */
	       && !is_positive_overflow_infinity (min))
	max = positive_overflow_infinity (type);
      else
	{
	  set_value_range_to_varying (vr);
	  return;
	}

      cmp = compare_values (min, max);

      /* If a VR_ANTI_RANGEs contains zero, then we have
	 ~[-INF, min(MIN, MAX)].  */
      if (vr0.type == VR_ANTI_RANGE)
	{
	  if (range_includes_zero_p (&vr0))
	    {
	      /* Take the lower of the two values.  */
	      if (cmp != 1)
		max = min;

	      /* Create ~[-INF, min (abs(MIN), abs(MAX))]
	         or ~[-INF + 1, min (abs(MIN), abs(MAX))] when
		 flag_wrapv is set and the original anti-range doesn't include
	         TYPE_MIN_VALUE, remember -TYPE_MIN_VALUE = TYPE_MIN_VALUE.  */
	      if (TYPE_OVERFLOW_WRAPS (type))
		{
		  tree type_min_value = TYPE_MIN_VALUE (type);

		  min = (vr0.min != type_min_value
			 ? int_const_binop (PLUS_EXPR, type_min_value,
					    integer_one_node, 0)
			 : type_min_value);
		}
	      else
		{
		  if (overflow_infinity_range_p (&vr0))
		    min = negative_overflow_infinity (type);
		  else
		    min = TYPE_MIN_VALUE (type);
		}
	    }
	  else
	    {
	      /* All else has failed, so create the range [0, INF], even for
	         flag_wrapv since TYPE_MIN_VALUE is in the original
	         anti-range.  */
	      vr0.type = VR_RANGE;
	      min = build_int_cst (type, 0);
	      if (needs_overflow_infinity (type))
		{
		  if (supports_overflow_infinity (type))
		    max = positive_overflow_infinity (type);
		  else
		    {
		      set_value_range_to_varying (vr);
		      return;
		    }
		}
	      else
		max = TYPE_MAX_VALUE (type);
	    }
	}

      /* If the range contains zero then we know that the minimum value in the
         range will be zero.  */
      else if (range_includes_zero_p (&vr0))
	{
	  if (cmp == 1)
	    max = min;
	  min = build_int_cst (type, 0);
	}
      else
	{
          /* If the range was reversed, swap MIN and MAX.  */
	  if (cmp == 1)
	    {
	      tree t = min;
	      min = max;
	      max = t;
	    }
	}
    }
  else
    {
      /* Otherwise, operate on each end of the range.  */
      min = fold_unary_to_constant (code, type, vr0.min);
      max = fold_unary_to_constant (code, type, vr0.max);

      if (needs_overflow_infinity (type))
	{
	  gcc_assert (code != NEGATE_EXPR && code != ABS_EXPR);

	  /* If both sides have overflowed, we don't know
	     anything.  */
	  if ((is_overflow_infinity (vr0.min)
	       || TREE_OVERFLOW (min))
	      && (is_overflow_infinity (vr0.max)
		  || TREE_OVERFLOW (max)))
	    {
	      set_value_range_to_varying (vr);
	      return;
	    }

	  if (is_overflow_infinity (vr0.min))
	    min = vr0.min;
	  else if (TREE_OVERFLOW (min))
	    {
	      if (supports_overflow_infinity (type))
		min = (tree_int_cst_sgn (min) >= 0
		       ? positive_overflow_infinity (TREE_TYPE (min))
		       : negative_overflow_infinity (TREE_TYPE (min)));
	      else
		{
		  set_value_range_to_varying (vr);
		  return;
		}
	    }

	  if (is_overflow_infinity (vr0.max))
	    max = vr0.max;
	  else if (TREE_OVERFLOW (max))
	    {
	      if (supports_overflow_infinity (type))
		max = (tree_int_cst_sgn (max) >= 0
		       ? positive_overflow_infinity (TREE_TYPE (max))
		       : negative_overflow_infinity (TREE_TYPE (max)));
	      else
		{
		  set_value_range_to_varying (vr);
		  return;
		}
	    }
	}
    }

  cmp = compare_values (min, max);
  if (cmp == -2 || cmp == 1)
    {
      /* If the new range has its limits swapped around (MIN > MAX),
	 then the operation caused one of them to wrap around, mark
	 the new range VARYING.  */
      set_value_range_to_varying (vr);
    }
  else
    set_value_range (vr, vr0.type, min, max, NULL);
}


/* Extract range information from a conditional expression EXPR based on
   the ranges of each of its operands and the expression code.  */

static void
extract_range_from_cond_expr (value_range_t *vr, tree expr)
{
  tree op0, op1;
  value_range_t vr0 = { VR_UNDEFINED, NULL_TREE, NULL_TREE, NULL };
  value_range_t vr1 = { VR_UNDEFINED, NULL_TREE, NULL_TREE, NULL };

  /* Get value ranges for each operand.  For constant operands, create
     a new value range with the operand to simplify processing.  */
  op0 = COND_EXPR_THEN (expr);
  if (TREE_CODE (op0) == SSA_NAME)
    vr0 = *(get_value_range (op0));
  else if (is_gimple_min_invariant (op0))
    set_value_range_to_value (&vr0, op0, NULL);
  else
    set_value_range_to_varying (&vr0);

  op1 = COND_EXPR_ELSE (expr);
  if (TREE_CODE (op1) == SSA_NAME)
    vr1 = *(get_value_range (op1));
  else if (is_gimple_min_invariant (op1))
    set_value_range_to_value (&vr1, op1, NULL);
  else
    set_value_range_to_varying (&vr1);

  /* The resulting value range is the union of the operand ranges */
  vrp_meet (&vr0, &vr1);
  copy_value_range (vr, &vr0);
}


/* Extract range information from a comparison expression EXPR based
   on the range of its operand and the expression code.  */

static void
extract_range_from_comparison (value_range_t *vr, enum tree_code code,
			       tree type, tree op0, tree op1)
{
  bool sop = false;
  tree val;

  val = vrp_evaluate_conditional_warnv_with_ops (code, op0, op1, false, &sop,
  						 NULL);

  /* A disadvantage of using a special infinity as an overflow
     representation is that we lose the ability to record overflow
     when we don't have an infinity.  So we have to ignore a result
     which relies on overflow.  */

  if (val && !is_overflow_infinity (val) && !sop)
    {
      /* Since this expression was found on the RHS of an assignment,
	 its type may be different from _Bool.  Convert VAL to EXPR's
	 type.  */
      val = fold_convert (type, val);
      if (is_gimple_min_invariant (val))
	set_value_range_to_value (vr, val, vr->equiv);
      else
	set_value_range (vr, VR_RANGE, val, val, vr->equiv);
    }
  else
    /* The result of a comparison is always true or false.  */
    set_value_range_to_truthvalue (vr, type);
}

/* Try to derive a nonnegative or nonzero range out of STMT relying
   primarily on generic routines in fold in conjunction with range data.
   Store the result in *VR */

static void
extract_range_basic (value_range_t *vr, GIMPLE_type stmt)
{
  bool sop = false;
  tree type = gimple_expr_type (stmt);

  if (INTEGRAL_TYPE_P (type)
      && gimple_stmt_nonnegative_warnv_p (stmt, &sop))
    set_value_range_to_nonnegative (vr, type,
				    sop || stmt_overflow_infinity (stmt));
  else if (vrp_stmt_computes_nonzero (stmt, &sop)
	   && !sop)
    set_value_range_to_nonnull (vr, type);
  else
    set_value_range_to_varying (vr);
}


/* Try to compute a useful range out of assignment STMT and store it
   in *VR.  */

static void
extract_range_from_assignment (value_range_t *vr, GIMPLE_type stmt)
{
  enum tree_code code = gimple_assign_rhs_code (stmt);

  if (code == ASSERT_EXPR)
    extract_range_from_assert (vr, gimple_assign_rhs1 (stmt));
  else if (code == SSA_NAME)
    extract_range_from_ssa_name (vr, gimple_assign_rhs1 (stmt));
  else if (TREE_CODE_CLASS (code) == tcc_binary
	   || code == TRUTH_AND_EXPR
	   || code == TRUTH_OR_EXPR
	   || code == TRUTH_XOR_EXPR)
    extract_range_from_binary_expr (vr, gimple_assign_rhs_code (stmt),
				    gimple_expr_type (stmt),
				    gimple_assign_rhs1 (stmt),
				    gimple_assign_rhs2 (stmt));
  else if (TREE_CODE_CLASS (code) == tcc_unary)
    extract_range_from_unary_expr (vr, gimple_assign_rhs_code (stmt),
				   gimple_expr_type (stmt),
				   gimple_assign_rhs1 (stmt));
  else if (code == COND_EXPR)
    extract_range_from_cond_expr (vr, gimple_assign_rhs1 (stmt));
  else if (TREE_CODE_CLASS (code) == tcc_comparison)
    extract_range_from_comparison (vr, gimple_assign_rhs_code (stmt),
				   gimple_expr_type (stmt),
				   gimple_assign_rhs1 (stmt),
				   gimple_assign_rhs2 (stmt));
  else if (get_gimple_rhs_class (code) == GIMPLE_SINGLE_RHS
	   && is_gimple_min_invariant (gimple_assign_rhs1 (stmt)))
    set_value_range_to_value (vr, gimple_assign_rhs1 (stmt), NULL);
  else
    set_value_range_to_varying (vr);

  if (vr->type == VR_VARYING)
    extract_range_basic (vr, stmt);
}

/* Given a range VR, a LOOP and a variable VAR, determine whether it
   would be profitable to adjust VR using scalar evolution information
   for VAR.  If so, update VR with the new limits.  */

static void
adjust_range_with_scev (value_range_t *vr, struct loop *loop,
         GIMPLE_type stmt, tree var)
{
  tree init, step, chrec, tmin, tmax, min, max, type, tem;
  enum ev_direction dir;

  /* TODO.  Don't adjust anti-ranges.  An anti-range may provide
     better opportunities than a regular range, but I'm not sure.  */
  if (vr->type == VR_ANTI_RANGE)
    return;

  chrec = instantiate_parameters (loop, analyze_scalar_evolution (loop, var));

  /* Like in PR19590, scev can return a constant function.  */
  if (is_gimple_min_invariant (chrec))
    {
      set_value_range_to_value (vr, chrec, vr->equiv);
      return;
    }

  if (TREE_CODE (chrec) != POLYNOMIAL_CHREC)
    return;

  init = initial_condition_in_loop_num (chrec, loop->num);
  tem = op_with_constant_singleton_value_range (init);
  if (tem)
    init = tem;
  step = evolution_part_in_loop_num (chrec, loop->num);
  tem = op_with_constant_singleton_value_range (step);
  if (tem)
    step = tem;

  /* If STEP is symbolic, we can't know whether INIT will be the
     minimum or maximum value in the range.  Also, unless INIT is
     a simple expression, compare_values and possibly other functions
     in tree-vrp won't be able to handle it.  */
  if (step == NULL_TREE
      || !is_gimple_min_invariant (step)
      || !valid_value_p (init))
    return;

  dir = scev_direction (chrec);
  if (/* Do not adjust ranges if we do not know whether the iv increases
	 or decreases,  ... */
      dir == EV_DIR_UNKNOWN
      /* ... or if it may wrap.  */
      || scev_probably_wraps_p (init, step, stmt, get_chrec_loop (chrec),
				true))
    return;

  /* We use TYPE_MIN_VALUE and TYPE_MAX_VALUE here instead of
     negative_overflow_infinity and positive_overflow_infinity,
     because we have concluded that the loop probably does not
     wrap.  */

  type = TREE_TYPE (var);
  if (POINTER_TYPE_P (type) || !TYPE_MIN_VALUE (type))
    tmin = lower_bound_in_type (type, type);
  else
    tmin = TYPE_MIN_VALUE (type);
  if (POINTER_TYPE_P (type) || !TYPE_MAX_VALUE (type))
    tmax = upper_bound_in_type (type, type);
  else
    tmax = TYPE_MAX_VALUE (type);

  /* Try to use estimated number of iterations for the loop to constrain the
     final value in the evolution.
     We are interested in the number of executions of the latch, while
     nb_iterations_upper_bound includes the last execution of the exit test.  */
  if (TREE_CODE (step) == INTEGER_CST
      && loop->any_upper_bound
      && !double_int_zero_p (loop->nb_iterations_upper_bound)
      && is_gimple_val (init)
      && (TREE_CODE (init) != SSA_NAME
	  || get_value_range (init)->type == VR_RANGE))
    {
      value_range_t maxvr = { VR_UNDEFINED, NULL_TREE, NULL_TREE, NULL };
      double_int dtmp;
      bool unsigned_p = TYPE_UNSIGNED (TREE_TYPE (step));
      int overflow = 0;

      dtmp = double_int_mul_with_sign (tree_to_double_int (step),
                                       double_int_sub (
                                           loop->nb_iterations_upper_bound,
                                           double_int_one),
                                       unsigned_p, &overflow);
      /* If the multiplication overflowed we can't do a meaningful
	 adjustment.  Likewise if the result doesn't fit in the type
	 of the induction variable.  For a signed type we have to
	 check whether the result has the expected signedness which
	 is that of the step as nb_iterations_upper_bound is unsigned.  */
      if (!overflow
	  && double_int_fits_to_tree_p (TREE_TYPE (init), dtmp)
	  && (unsigned_p
	      || ((dtmp.high ^ TREE_INT_CST_HIGH (step)) >= 0)))
	{
	  tem = double_int_to_tree (TREE_TYPE (init), dtmp);
	  extract_range_from_binary_expr (&maxvr, PLUS_EXPR,
					  TREE_TYPE (init), init, tem);
	  /* Likewise if the addition did.  */
	  if (maxvr.type == VR_RANGE)
	    {
	      tmin = maxvr.min;
	      tmax = maxvr.max;
	    }
	}
    }

  if (vr->type == VR_VARYING || vr->type == VR_UNDEFINED)
    {
      min = tmin;
      max = tmax;

      /* For VARYING or UNDEFINED ranges, just about anything we get
	 from scalar evolutions should be better.  */

      if (dir == EV_DIR_DECREASES)
	max = init;
      else
	min = init;

      /* If we would create an invalid range, then just assume we
	 know absolutely nothing.  This may be over-conservative,
	 but it's clearly safe, and should happen only in unreachable
         parts of code, or for invalid programs.  */
      if (compare_values (min, max) == 1)
	return;

      set_value_range (vr, VR_RANGE, min, max, vr->equiv);
    }
  else if (vr->type == VR_RANGE)
    {
      min = vr->min;
      max = vr->max;

      if (dir == EV_DIR_DECREASES)
	{
	  /* INIT is the maximum value.  If INIT is lower than VR->MAX
	     but no smaller than VR->MIN, set VR->MAX to INIT.  */
	  if (compare_values (init, max) == -1)
	    max = init;

	  /* According to the loop information, the variable does not
	     overflow.  If we think it does, probably because of an
	     overflow due to arithmetic on a different INF value,
	     reset now.  */
	  if (is_negative_overflow_infinity (min)
	      || compare_values (min, tmin) == -1)
	    min = tmin;

	}
      else
	{
	  /* If INIT is bigger than VR->MIN, set VR->MIN to INIT.  */
	  if (compare_values (init, min) == 1)
	    min = init;

	  if (is_positive_overflow_infinity (max)
	      || compare_values (tmax, max) == -1)
	    max = tmax;
	}

      /* If we just created an invalid range with the minimum
	 greater than the maximum, we fail conservatively.
	 This should happen only in unreachable
	 parts of code, or for invalid programs.  */
      if (compare_values (min, max) == 1)
	return;

      set_value_range (vr, VR_RANGE, min, max, vr->equiv);
    }
}

/* Return true if VAR may overflow at STMT.  This checks any available
   loop information to see if we can determine that VAR does not
   overflow.  */

static bool
vrp_var_may_overflow (tree var, GIMPLE_type stmt)
{
  struct loop *l;
  tree chrec, init, step;

  if (current_loops == NULL)
    return true;

  l = loop_containing_stmt (stmt);
  if (l == NULL
      || !loop_outer (l))
    return true;

  chrec = instantiate_parameters (l, analyze_scalar_evolution (l, var));
  if (TREE_CODE (chrec) != POLYNOMIAL_CHREC)
    return true;

  init = initial_condition_in_loop_num (chrec, l->num);
  step = evolution_part_in_loop_num (chrec, l->num);

  if (step == NULL_TREE
      || !is_gimple_min_invariant (step)
      || !valid_value_p (init))
    return true;

  /* If we get here, we know something useful about VAR based on the
     loop information.  If it wraps, it may overflow.  */

  if (scev_probably_wraps_p (init, step, stmt, get_chrec_loop (chrec),
			     true))
    return true;

  if (dump_file && (dump_flags & TDF_DETAILS) != 0)
    {
      print_generic_expr (dump_file, var, 0);
      fprintf (dump_file, ": loop information indicates does not overflow\n");
    }

  return false;
}


/* Given two numeric value ranges VR0, VR1 and a comparison code COMP:

   - Return BOOLEAN_TRUE_NODE if VR0 COMP VR1 always returns true for
     all the values in the ranges.

   - Return BOOLEAN_FALSE_NODE if the comparison always returns false.

   - Return NULL_TREE if it is not always possible to determine the
     value of the comparison.

   Also set *STRICT_OVERFLOW_P to indicate whether a range with an
   overflow infinity was used in the test.  */


static tree
compare_ranges (enum tree_code comp, value_range_t *vr0, value_range_t *vr1,
		bool *strict_overflow_p)
{
  /* VARYING or UNDEFINED ranges cannot be compared.  */
  if (vr0->type == VR_VARYING
      || vr0->type == VR_UNDEFINED
      || vr1->type == VR_VARYING
      || vr1->type == VR_UNDEFINED)
    return NULL_TREE;

  /* Anti-ranges need to be handled separately.  */
  if (vr0->type == VR_ANTI_RANGE || vr1->type == VR_ANTI_RANGE)
    {
      /* If both are anti-ranges, then we cannot compute any
	 comparison.  */
      if (vr0->type == VR_ANTI_RANGE && vr1->type == VR_ANTI_RANGE)
	return NULL_TREE;

      /* These comparisons are never statically computable.  */
      if (comp == GT_EXPR
	  || comp == GE_EXPR
	  || comp == LT_EXPR
	  || comp == LE_EXPR)
	return NULL_TREE;

      /* Equality can be computed only between a range and an
	 anti-range.  ~[VAL1, VAL2] == [VAL1, VAL2] is always false.  */
      if (vr0->type == VR_RANGE)
	{
	  /* To simplify processing, make VR0 the anti-range.  */
	  value_range_t *tmp = vr0;
	  vr0 = vr1;
	  vr1 = tmp;
	}

      gcc_assert (comp == NE_EXPR || comp == EQ_EXPR);

      if (compare_values_warnv (vr0->min, vr1->min, strict_overflow_p) == 0
	  && compare_values_warnv (vr0->max, vr1->max, strict_overflow_p) == 0)
	return (comp == NE_EXPR) ? boolean_true_node : boolean_false_node;

      return NULL_TREE;
    }

  if (!usable_range_p (vr0, strict_overflow_p)
      || !usable_range_p (vr1, strict_overflow_p))
    return NULL_TREE;

  /* Simplify processing.  If COMP is GT_EXPR or GE_EXPR, switch the
     operands around and change the comparison code.  */
  if (comp == GT_EXPR || comp == GE_EXPR)
    {
      value_range_t *tmp;
      comp = (comp == GT_EXPR) ? LT_EXPR : LE_EXPR;
      tmp = vr0;
      vr0 = vr1;
      vr1 = tmp;
    }

  if (comp == EQ_EXPR)
    {
      /* Equality may only be computed if both ranges represent
	 exactly one value.  */
      if (compare_values_warnv (vr0->min, vr0->max, strict_overflow_p) == 0
	  && compare_values_warnv (vr1->min, vr1->max, strict_overflow_p) == 0)
	{
	  int cmp_min = compare_values_warnv (vr0->min, vr1->min,
					      strict_overflow_p);
	  int cmp_max = compare_values_warnv (vr0->max, vr1->max,
					      strict_overflow_p);
	  if (cmp_min == 0 && cmp_max == 0)
	    return boolean_true_node;
	  else if (cmp_min != -2 && cmp_max != -2)
	    return boolean_false_node;
	}
      /* If [V0_MIN, V1_MAX] < [V1_MIN, V1_MAX] then V0 != V1.  */
      else if (compare_values_warnv (vr0->min, vr1->max,
				     strict_overflow_p) == 1
	       || compare_values_warnv (vr1->min, vr0->max,
					strict_overflow_p) == 1)
	return boolean_false_node;

      return NULL_TREE;
    }
  else if (comp == NE_EXPR)
    {
      int cmp1, cmp2;

      /* If VR0 is completely to the left or completely to the right
	 of VR1, they are always different.  Notice that we need to
	 make sure that both comparisons yield similar results to
	 avoid comparing values that cannot be compared at
	 compile-time.  */
      cmp1 = compare_values_warnv (vr0->max, vr1->min, strict_overflow_p);
      cmp2 = compare_values_warnv (vr0->min, vr1->max, strict_overflow_p);
      if ((cmp1 == -1 && cmp2 == -1) || (cmp1 == 1 && cmp2 == 1))
	return boolean_true_node;

      /* If VR0 and VR1 represent a single value and are identical,
	 return false.  */
      else if (compare_values_warnv (vr0->min, vr0->max,
				     strict_overflow_p) == 0
	       && compare_values_warnv (vr1->min, vr1->max,
					strict_overflow_p) == 0
	       && compare_values_warnv (vr0->min, vr1->min,
					strict_overflow_p) == 0
	       && compare_values_warnv (vr0->max, vr1->max,
					strict_overflow_p) == 0)
	return boolean_false_node;

      /* Otherwise, they may or may not be different.  */
      else
	return NULL_TREE;
    }
  else if (comp == LT_EXPR || comp == LE_EXPR)
    {
      int tst;

      /* If VR0 is to the left of VR1, return true.  */
      tst = compare_values_warnv (vr0->max, vr1->min, strict_overflow_p);
      if ((comp == LT_EXPR && tst == -1)
	  || (comp == LE_EXPR && (tst == -1 || tst == 0)))
	{
	  if (overflow_infinity_range_p (vr0)
	      || overflow_infinity_range_p (vr1))
	    *strict_overflow_p = true;
	  return boolean_true_node;
	}

      /* If VR0 is to the right of VR1, return false.  */
      tst = compare_values_warnv (vr0->min, vr1->max, strict_overflow_p);
      if ((comp == LT_EXPR && (tst == 0 || tst == 1))
	  || (comp == LE_EXPR && tst == 1))
	{
	  if (overflow_infinity_range_p (vr0)
	      || overflow_infinity_range_p (vr1))
	    *strict_overflow_p = true;
	  return boolean_false_node;
	}

      /* Otherwise, we don't know.  */
      return NULL_TREE;
    }

  gcc_unreachable ();
}


/* Given a value range VR, a value VAL and a comparison code COMP, return
   BOOLEAN_TRUE_NODE if VR COMP VAL always returns true for all the
   values in VR.  Return BOOLEAN_FALSE_NODE if the comparison
   always returns false.  Return NULL_TREE if it is not always
   possible to determine the value of the comparison.  Also set
   *STRICT_OVERFLOW_P to indicate whether a range with an overflow
   infinity was used in the test.  */

static tree
compare_range_with_value (enum tree_code comp, value_range_t *vr, tree val,
			  bool *strict_overflow_p)
{
  if (vr->type == VR_VARYING || vr->type == VR_UNDEFINED)
    return NULL_TREE;

  /* Anti-ranges need to be handled separately.  */
  if (vr->type == VR_ANTI_RANGE)
    {
      /* For anti-ranges, the only predicates that we can compute at
	 compile time are equality and inequality.  */
      if (comp == GT_EXPR
	  || comp == GE_EXPR
	  || comp == LT_EXPR
	  || comp == LE_EXPR)
	return NULL_TREE;

      /* ~[VAL_1, VAL_2] OP VAL is known if VAL_1 <= VAL <= VAL_2.  */
      if (value_inside_range (val, vr) == 1)
	return (comp == NE_EXPR) ? boolean_true_node : boolean_false_node;

      return NULL_TREE;
    }

  if (!usable_range_p (vr, strict_overflow_p))
    return NULL_TREE;

  if (comp == EQ_EXPR)
    {
      /* EQ_EXPR may only be computed if VR represents exactly
	 one value.  */
      if (compare_values_warnv (vr->min, vr->max, strict_overflow_p) == 0)
	{
	  int cmp = compare_values_warnv (vr->min, val, strict_overflow_p);
	  if (cmp == 0)
	    return boolean_true_node;
	  else if (cmp == -1 || cmp == 1 || cmp == 2)
	    return boolean_false_node;
	}
      else if (compare_values_warnv (val, vr->min, strict_overflow_p) == -1
	       || compare_values_warnv (vr->max, val, strict_overflow_p) == -1)
	return boolean_false_node;

      return NULL_TREE;
    }
  else if (comp == NE_EXPR)
    {
      /* If VAL is not inside VR, then they are always different.  */
      if (compare_values_warnv (vr->max, val, strict_overflow_p) == -1
	  || compare_values_warnv (vr->min, val, strict_overflow_p) == 1)
	return boolean_true_node;

      /* If VR represents exactly one value equal to VAL, then return
	 false.  */
      if (compare_values_warnv (vr->min, vr->max, strict_overflow_p) == 0
	  && compare_values_warnv (vr->min, val, strict_overflow_p) == 0)
	return boolean_false_node;

      /* Otherwise, they may or may not be different.  */
      return NULL_TREE;
    }
  else if (comp == LT_EXPR || comp == LE_EXPR)
    {
      int tst;

      /* If VR is to the left of VAL, return true.  */
      tst = compare_values_warnv (vr->max, val, strict_overflow_p);
      if ((comp == LT_EXPR && tst == -1)
	  || (comp == LE_EXPR && (tst == -1 || tst == 0)))
	{
	  if (overflow_infinity_range_p (vr))
	    *strict_overflow_p = true;
	  return boolean_true_node;
	}

      /* If VR is to the right of VAL, return false.  */
      tst = compare_values_warnv (vr->min, val, strict_overflow_p);
      if ((comp == LT_EXPR && (tst == 0 || tst == 1))
	  || (comp == LE_EXPR && tst == 1))
	{
	  if (overflow_infinity_range_p (vr))
	    *strict_overflow_p = true;
	  return boolean_false_node;
	}

      /* Otherwise, we don't know.  */
      return NULL_TREE;
    }
  else if (comp == GT_EXPR || comp == GE_EXPR)
    {
      int tst;

      /* If VR is to the right of VAL, return true.  */
      tst = compare_values_warnv (vr->min, val, strict_overflow_p);
      if ((comp == GT_EXPR && tst == 1)
	  || (comp == GE_EXPR && (tst == 0 || tst == 1)))
	{
	  if (overflow_infinity_range_p (vr))
	    *strict_overflow_p = true;
	  return boolean_true_node;
	}

      /* If VR is to the left of VAL, return false.  */
      tst = compare_values_warnv (vr->max, val, strict_overflow_p);
      if ((comp == GT_EXPR && (tst == -1 || tst == 0))
	  || (comp == GE_EXPR && tst == -1))
	{
	  if (overflow_infinity_range_p (vr))
	    *strict_overflow_p = true;
	  return boolean_false_node;
	}

      /* Otherwise, we don't know.  */
      return NULL_TREE;
    }

  gcc_unreachable ();
}


/* Debugging dumps.  */

static void dump_value_range (FILE *, value_range_t *);
static void debug_value_range (value_range_t *);
static void dump_all_value_ranges (FILE *);
static void debug_all_value_ranges (void);
static void dump_vr_equiv (FILE *, bitmap);
static void debug_vr_equiv (bitmap);


/* Dump value range VR to FILE.  */

static void
dump_value_range (FILE *file, value_range_t *vr)
{
  if (vr == NULL)
    fprintf (file, "[]");
  else if (vr->type == VR_UNDEFINED)
    fprintf (file, "UNDEFINED");
  else if (vr->type == VR_RANGE || vr->type == VR_ANTI_RANGE)
    {
      tree type = TREE_TYPE (vr->min);

      fprintf (file, "%s[", (vr->type == VR_ANTI_RANGE) ? "~" : "");

      if (is_negative_overflow_infinity (vr->min))
	fprintf (file, "-INF(OVF)");
      else if (INTEGRAL_TYPE_P (type)
	       && !TYPE_UNSIGNED (type)
	       && vrp_val_is_min (vr->min))
	fprintf (file, "-INF");
      else
	print_generic_expr (file, vr->min, 0);

      fprintf (file, ", ");

      if (is_positive_overflow_infinity (vr->max))
	fprintf (file, "+INF(OVF)");
      else if (INTEGRAL_TYPE_P (type)
	       && vrp_val_is_max (vr->max))
	fprintf (file, "+INF");
      else
	print_generic_expr (file, vr->max, 0);

      fprintf (file, "]");

      if (vr->equiv)
	{
	  bitmap_iterator bi;
	  unsigned i, c = 0;

	  fprintf (file, "  EQUIVALENCES: { ");

	  EXECUTE_IF_SET_IN_BITMAP (vr->equiv, 0, i, bi)
	    {
	      print_generic_expr (file, ssa_name (i), 0);
	      fprintf (file, " ");
	      c++;
	    }

	  fprintf (file, "} (%u elements)", c);
	}
    }
  else if (vr->type == VR_VARYING)
    fprintf (file, "VARYING");
  else
    fprintf (file, "INVALID RANGE");
}


/* Dump value range VR to stderr.  */

static void
debug_value_range (value_range_t *vr)
{
  dump_value_range (stderr, vr);
  fprintf (stderr, "\n");
}


/* Dump value ranges of all SSA_NAMEs to FILE.  */

static void
dump_all_value_ranges (FILE *file)
{
  size_t i;

  for (i = 0; i < num_ssa_names; i++)
    {
      if (vr_value[i])
	{
	  print_generic_expr (file, ssa_name (i), 0);
	  fprintf (file, ": ");
	  dump_value_range (file, vr_value[i]);
	  fprintf (file, "\n");
	}
    }

  fprintf (file, "\n");
}


/* Dump all value ranges to stderr.  */

static void
debug_all_value_ranges (void)
{
  dump_all_value_ranges (stderr);
}


/* Given a COND_EXPR COND of the form 'V OP W', and an SSA name V,
   create a new SSA name N and return the assertion assignment
   'V = ASSERT_EXPR <V, V OP W>'.  */

static GIMPLE_type
build_assert_expr_for (tree cond, tree v)
{
  tree n;
  GIMPLE_type assertion;

  gcc_assert (TREE_CODE (v) == SSA_NAME);
  n = duplicate_ssa_name (v, NULL);

  if (COMPARISON_CLASS_P (cond))
    {
      tree a = build2 (ASSERT_EXPR, TREE_TYPE (v), v, cond);
      assertion = gimple_build_assign (n, a);
    }
  else if (TREE_CODE (cond) == TRUTH_NOT_EXPR)
    {
      /* Given !V, build the assignment N = false.  */
      tree op0 = TREE_OPERAND (cond, 0);
      gcc_assert (op0 == v);
      assertion = gimple_build_assign (n, boolean_false_node);
    }
  else if (TREE_CODE (cond) == SSA_NAME)
    {
      /* Given V, build the assignment N = true.  */
      gcc_assert (v == cond);
      assertion = gimple_build_assign (n, boolean_true_node);
    }
  else
    gcc_unreachable ();

  SSA_NAME_DEF_STMT (n) = assertion;

  /* The new ASSERT_EXPR, creates a new SSA name that replaces the
     operand of the ASSERT_EXPR. Register the new name and the old one
     in the replacement table so that we can fix the SSA web after
     adding all the ASSERT_EXPRs.  */
  register_new_name_mapping (n, v);

  return assertion;
}


/* Return false if EXPR is a predicate expression involving floating
   point values.  */

static inline bool
fp_predicate (GIMPLE_type stmt)
{
  GIMPLE_CHECK (stmt, GIMPLE_COND);

  return FLOAT_TYPE_P (TREE_TYPE (gimple_cond_lhs (stmt)));
}


/* If the range of values taken by OP can be inferred after STMT executes,
   return the comparison code (COMP_CODE_P) and value (VAL_P) that
   describes the inferred range.  Return true if a range could be
   inferred.  */

static bool
infer_value_range (GIMPLE_type stmt, tree op, enum tree_code *comp_code_p, tree *val_p)
{
  *val_p = NULL_TREE;
  *comp_code_p = ERROR_MARK;

  /* Do not attempt to infer anything in names that flow through
     abnormal edges.  */
  if (SSA_NAME_OCCURS_IN_ABNORMAL_PHI (op))
    return false;

  /* Similarly, don't infer anything from statements that may throw
     exceptions.  */
  if (stmt_could_throw_p (stmt))
    return false;

  /* If STMT is the last statement of a basic block with no
     successors, there is no point inferring anything about any of its
     operands.  We would not be able to find a proper insertion point
     for the assertion, anyway.  */
  if (stmt_ends_bb_p (stmt) && EDGE_COUNT (gimple_bb (stmt)->succs) == 0)
    return false;

  /* We can only assume that a pointer dereference will yield
     non-NULL if -fdelete-null-pointer-checks is enabled.  */
  if (flag_delete_null_pointer_checks
      && POINTER_TYPE_P (TREE_TYPE (op))
      && gimple_code (stmt) != GIMPLE_ASM)
    {
      unsigned num_uses, num_loads, num_stores;

      count_uses_and_derefs (op, stmt, &num_uses, &num_loads, &num_stores);
      if (num_loads + num_stores > 0)
	{
	  *val_p = build_int_cst (TREE_TYPE (op), 0);
	  *comp_code_p = NE_EXPR;
	  return true;
	}
    }

  return false;
}


static void dump_asserts_for (FILE *, tree);
static void debug_asserts_for (tree);
static void dump_all_asserts (FILE *);
static void debug_all_asserts (void);

/* Dump all the registered assertions for NAME to FILE.  */

static void
dump_asserts_for (FILE *file, tree name)
{
  assert_locus_t loc;

  fprintf (file, "Assertions to be inserted for ");
  print_generic_expr (file, name, 0);
  fprintf (file, "\n");

  loc = asserts_for[SSA_NAME_VERSION (name)];
  while (loc)
    {
      fprintf (file, "\t");
      print_gimple_stmt (file, gsi_stmt (loc->si), 0, 0);
      fprintf (file, "\n\tBB #%d", loc->bb->index);
      if (loc->e)
	{
	  fprintf (file, "\n\tEDGE %d->%d", loc->e->src->index,
	           loc->e->dest->index);
	  dump_edge_info (file, loc->e, 0);
	}
      fprintf (file, "\n\tPREDICATE: ");
      print_generic_expr (file, name, 0);
      fprintf (file, " %s ", GET_TREE_CODE_NAME(loc->comp_code));
      print_generic_expr (file, loc->val, 0);
      fprintf (file, "\n\n");
      loc = loc->next;
    }

  fprintf (file, "\n");
}


/* Dump all the registered assertions for NAME to stderr.  */

static void
debug_asserts_for (tree name)
{
  dump_asserts_for (stderr, name);
}


/* Dump all the registered assertions for all the names to FILE.  */

static void
dump_all_asserts (FILE *file)
{
  unsigned i;
  bitmap_iterator bi;

  fprintf (file, "\nASSERT_EXPRs to be inserted\n\n");
  EXECUTE_IF_SET_IN_BITMAP (need_assert_for, 0, i, bi)
    dump_asserts_for (file, ssa_name (i));
  fprintf (file, "\n");
}


/* Dump all the registered assertions for all the names to stderr.  */

static void
debug_all_asserts (void)
{
  dump_all_asserts (stderr);
}


/* If NAME doesn't have an ASSERT_EXPR registered for asserting
   'EXPR COMP_CODE VAL' at a location that dominates block BB or
   E->DEST, then register this location as a possible insertion point
   for ASSERT_EXPR <NAME, EXPR COMP_CODE VAL>.

   BB, E and SI provide the exact insertion point for the new
   ASSERT_EXPR.  If BB is NULL, then the ASSERT_EXPR is to be inserted
   on edge E.  Otherwise, if E is NULL, the ASSERT_EXPR is inserted on
   BB.  If SI points to a COND_EXPR or a SWITCH_EXPR statement, then E
   must not be NULL.  */

static void
register_new_assert_for (tree name, tree expr,
			 enum tree_code comp_code,
			 tree val,
			 basic_block bb,
			 edge e,
			 gimple_stmt_iterator si)
{
  assert_locus_t n, loc, last_loc;
  basic_block dest_bb;

  gcc_checking_assert (bb == NULL || e == NULL);

  if (e == NULL)
    gcc_checking_assert (gimple_code (gsi_stmt (si)) != GIMPLE_COND
			 && gimple_code (gsi_stmt (si)) != GIMPLE_SWITCH);

  /* Never build an assert comparing against an integer constant with
     TREE_OVERFLOW set.  This confuses our undefined overflow warning
     machinery.  */
  if (TREE_CODE (val) == INTEGER_CST
      && TREE_OVERFLOW (val))
    val = build_int_cst_wide (TREE_TYPE (val),
			      TREE_INT_CST_LOW (val), TREE_INT_CST_HIGH (val));

  /* The new assertion A will be inserted at BB or E.  We need to
     determine if the new location is dominated by a previously
     registered location for A.  If we are doing an edge insertion,
     assume that A will be inserted at E->DEST.  Note that this is not
     necessarily true.

     If E is a critical edge, it will be split.  But even if E is
     split, the new block will dominate the same set of blocks that
     E->DEST dominates.

     The reverse, however, is not true, blocks dominated by E->DEST
     will not be dominated by the new block created to split E.  So,
     if the insertion location is on a critical edge, we will not use
     the new location to move another assertion previously registered
     at a block dominated by E->DEST.  */
  dest_bb = (bb) ? bb : e->dest;

  /* If NAME already has an ASSERT_EXPR registered for COMP_CODE and
     VAL at a block dominating DEST_BB, then we don't need to insert a new
     one.  Similarly, if the same assertion already exists at a block
     dominated by DEST_BB and the new location is not on a critical
     edge, then update the existing location for the assertion (i.e.,
     move the assertion up in the dominance tree).

     Note, this is implemented as a simple linked list because there
     should not be more than a handful of assertions registered per
     name.  If this becomes a performance problem, a table hashed by
     COMP_CODE and VAL could be implemented.  */
  loc = asserts_for[SSA_NAME_VERSION (name)];
  last_loc = loc;
  while (loc)
    {
      if (loc->comp_code == comp_code
	  && (loc->val == val
	      || operand_equal_p (loc->val, val, 0))
	  && (loc->expr == expr
	      || operand_equal_p (loc->expr, expr, 0)))
	{
	  /* If the assertion NAME COMP_CODE VAL has already been
	     registered at a basic block that dominates DEST_BB, then
	     we don't need to insert the same assertion again.  Note
	     that we don't check strict dominance here to avoid
	     replicating the same assertion inside the same basic
	     block more than once (e.g., when a pointer is
	     dereferenced several times inside a block).

	     An exception to this rule are edge insertions.  If the
	     new assertion is to be inserted on edge E, then it will
	     dominate all the other insertions that we may want to
	     insert in DEST_BB.  So, if we are doing an edge
	     insertion, don't do this dominance check.  */
          if (e == NULL
	      && dominated_by_p (CDI_DOMINATORS, dest_bb, loc->bb))
	    return;

	  /* Otherwise, if E is not a critical edge and DEST_BB
	     dominates the existing location for the assertion, move
	     the assertion up in the dominance tree by updating its
	     location information.  */
	  if ((e == NULL || !EDGE_CRITICAL_P (e))
	      && dominated_by_p (CDI_DOMINATORS, loc->bb, dest_bb))
	    {
	      loc->bb = dest_bb;
	      loc->e = e;
	      loc->si = si;
	      return;
	    }
	}

      /* Update the last node of the list and move to the next one.  */
      last_loc = loc;
      loc = loc->next;
    }

  /* If we didn't find an assertion already registered for
     NAME COMP_CODE VAL, add a new one at the end of the list of
     assertions associated with NAME.  */
  n = XNEW (struct assert_locus_d);
  n->bb = dest_bb;
  n->e = e;
  n->si = si;
  n->comp_code = comp_code;
  n->val = val;
  n->expr = expr;
  n->next = NULL;

  if (last_loc)
    last_loc->next = n;
  else
    asserts_for[SSA_NAME_VERSION (name)] = n;

  bitmap_set_bit (need_assert_for, SSA_NAME_VERSION (name));
}

/* (COND_OP0 COND_CODE COND_OP1) is a predicate which uses NAME.
   Extract a suitable test code and value and store them into *CODE_P and
   *VAL_P so the predicate is normalized to NAME *CODE_P *VAL_P.

   If no extraction was possible, return FALSE, otherwise return TRUE.

   If INVERT is true, then we invert the result stored into *CODE_P.  */

static bool
extract_code_and_val_from_cond_with_ops (tree name, enum tree_code cond_code,
					 tree cond_op0, tree cond_op1,
					 bool invert, enum tree_code *code_p,
					 tree *val_p)
{
  enum tree_code comp_code;
  tree val;

  /* Otherwise, we have a comparison of the form NAME COMP VAL
     or VAL COMP NAME.  */
  if (name == cond_op1)
    {
      /* If the predicate is of the form VAL COMP NAME, flip
	 COMP around because we need to register NAME as the
	 first operand in the predicate.  */
      comp_code = swap_tree_comparison (cond_code);
      val = cond_op0;
    }
  else
    {
      /* The comparison is of the form NAME COMP VAL, so the
	 comparison code remains unchanged.  */
      comp_code = cond_code;
      val = cond_op1;
    }

  /* Invert the comparison code as necessary.  */
  if (invert)
    comp_code = invert_tree_comparison (comp_code, 0);

  /* VRP does not handle float types.  */
  if (SCALAR_FLOAT_TYPE_P (TREE_TYPE (val)))
    return false;

  /* Do not register always-false predicates.
     FIXME:  this works around a limitation in fold() when dealing with
     enumerations.  Given 'enum { N1, N2 } x;', fold will not
     fold 'if (x > N2)' to 'if (0)'.  */
  if ((comp_code == GT_EXPR || comp_code == LT_EXPR)
      && INTEGRAL_TYPE_P (TREE_TYPE (val)))
    {
      tree min = TYPE_MIN_VALUE (TREE_TYPE (val));
      tree max = TYPE_MAX_VALUE (TREE_TYPE (val));

      if (comp_code == GT_EXPR
	  && (!max
	      || compare_values (val, max) == 0))
	return false;

      if (comp_code == LT_EXPR
	  && (!min
	      || compare_values (val, min) == 0))
	return false;
    }
  *code_p = comp_code;
  *val_p = val;
  return true;
}

/* Try to register an edge assertion for SSA name NAME on edge E for
   the condition COND contributing to the conditional jump pointed to by BSI.
   Invert the condition COND if INVERT is true.
   Return true if an assertion for NAME could be registered.  */

static bool
register_edge_assert_for_2 (tree name, edge e, gimple_stmt_iterator bsi,
			    enum tree_code cond_code,
			    tree cond_op0, tree cond_op1, bool invert)
{
  tree val;
  enum tree_code comp_code;
  bool retval = false;

  if (!extract_code_and_val_from_cond_with_ops (name, cond_code,
						cond_op0,
						cond_op1,
						invert, &comp_code, &val))
    return false;

  /* Only register an ASSERT_EXPR if NAME was found in the sub-graph
     reachable from E.  */
  if (live_on_edge (e, name)
      && !has_single_use (name))
    {
      register_new_assert_for (name, name, comp_code, val, NULL, e, bsi);
      retval = true;
    }

  /* In the case of NAME <= CST and NAME being defined as
     NAME = (unsigned) NAME2 + CST2 we can assert NAME2 >= -CST2
     and NAME2 <= CST - CST2.  We can do the same for NAME > CST.
     This catches range and anti-range tests.  */
  if ((comp_code == LE_EXPR
       || comp_code == GT_EXPR)
      && TREE_CODE (val) == INTEGER_CST
      && TYPE_UNSIGNED (TREE_TYPE (val)))
    {
      GIMPLE_type def_stmt = SSA_NAME_DEF_STMT (name);
      tree cst2 = NULL_TREE, name2 = NULL_TREE, name3 = NULL_TREE;

      /* Extract CST2 from the (optional) addition.  */
      if (is_gimple_assign (def_stmt)
	  && gimple_assign_rhs_code (def_stmt) == PLUS_EXPR)
	{
	  name2 = gimple_assign_rhs1 (def_stmt);
	  cst2 = gimple_assign_rhs2 (def_stmt);
	  if (TREE_CODE (name2) == SSA_NAME
	      && TREE_CODE (cst2) == INTEGER_CST)
	    def_stmt = SSA_NAME_DEF_STMT (name2);
	}

      /* Extract NAME2 from the (optional) sign-changing cast.  */
      if (gimple_assign_cast_p (def_stmt))
	{
	  if (CONVERT_EXPR_CODE_P (gimple_assign_rhs_code (def_stmt))
	      && ! TYPE_UNSIGNED (TREE_TYPE (gimple_assign_rhs1 (def_stmt)))
	      && (TYPE_PRECISION (gimple_expr_type (def_stmt))
		  == TYPE_PRECISION (TREE_TYPE (gimple_assign_rhs1 (def_stmt)))))
	    name3 = gimple_assign_rhs1 (def_stmt);
	}

      /* If name3 is used later, create an ASSERT_EXPR for it.  */
      if (name3 != NULL_TREE
      	  && TREE_CODE (name3) == SSA_NAME
	  && (cst2 == NULL_TREE
	      || TREE_CODE (cst2) == INTEGER_CST)
	  && INTEGRAL_TYPE_P (TREE_TYPE (name3))
	  && live_on_edge (e, name3)
	  && !has_single_use (name3))
	{
	  tree tmp;

	  /* Build an expression for the range test.  */
	  tmp = build1 (NOP_EXPR, TREE_TYPE (name), name3);
	  if (cst2 != NULL_TREE)
	    tmp = build2 (PLUS_EXPR, TREE_TYPE (name), tmp, cst2);

	  if (dump_file)
	    {
	      fprintf (dump_file, "Adding assert for ");
	      print_generic_expr (dump_file, name3, 0);
	      fprintf (dump_file, " from ");
	      print_generic_expr (dump_file, tmp, 0);
	      fprintf (dump_file, "\n");
	    }

	  register_new_assert_for (name3, tmp, comp_code, val, NULL, e, bsi);

	  retval = true;
	}

      /* If name2 is used later, create an ASSERT_EXPR for it.  */
      if (name2 != NULL_TREE
      	  && TREE_CODE (name2) == SSA_NAME
	  && TREE_CODE (cst2) == INTEGER_CST
	  && INTEGRAL_TYPE_P (TREE_TYPE (name2))
	  && live_on_edge (e, name2)
	  && !has_single_use (name2))
	{
	  tree tmp;

	  /* Build an expression for the range test.  */
	  tmp = name2;
	  if (TREE_TYPE (name) != TREE_TYPE (name2))
	    tmp = build1 (NOP_EXPR, TREE_TYPE (name), tmp);
	  if (cst2 != NULL_TREE)
	    tmp = build2 (PLUS_EXPR, TREE_TYPE (name), tmp, cst2);

	  if (dump_file)
	    {
	      fprintf (dump_file, "Adding assert for ");
	      print_generic_expr (dump_file, name2, 0);
	      fprintf (dump_file, " from ");
	      print_generic_expr (dump_file, tmp, 0);
	      fprintf (dump_file, "\n");
	    }

	  register_new_assert_for (name2, tmp, comp_code, val, NULL, e, bsi);

	  retval = true;
	}
    }

  return retval;
}

/* OP is an operand of a truth value expression which is known to have
   a particular value.  Register any asserts for OP and for any
   operands in OP's defining statement.

   If CODE is EQ_EXPR, then we want to register OP is zero (false),
   if CODE is NE_EXPR, then we want to register OP is nonzero (true).   */

static bool
register_edge_assert_for_1 (tree op, enum tree_code code,
			    edge e, gimple_stmt_iterator bsi)
{
  bool retval = false;
  GIMPLE_type op_def;
  tree val;
  enum tree_code rhs_code;

  /* We only care about SSA_NAMEs.  */
  if (TREE_CODE (op) != SSA_NAME)
    return false;

  /* We know that OP will have a zero or nonzero value.  If OP is used
     more than once go ahead and register an assert for OP.

     The FOUND_IN_SUBGRAPH support is not helpful in this situation as
     it will always be set for OP (because OP is used in a COND_EXPR in
     the subgraph).  */
  if (!has_single_use (op))
    {
      val = build_int_cst (TREE_TYPE (op), 0);
      register_new_assert_for (op, op, code, val, NULL, e, bsi);
      retval = true;
    }

  /* Now look at how OP is set.  If it's set from a comparison,
     a truth operation or some bit operations, then we may be able
     to register information about the operands of that assignment.  */
  op_def = SSA_NAME_DEF_STMT (op);
  if (gimple_code (op_def) != GIMPLE_ASSIGN)
    return retval;

  rhs_code = gimple_assign_rhs_code (op_def);

  if (TREE_CODE_CLASS (rhs_code) == tcc_comparison)
    {
      bool invert = (code == EQ_EXPR ? true : false);
      tree op0 = gimple_assign_rhs1 (op_def);
      tree op1 = gimple_assign_rhs2 (op_def);

      if (TREE_CODE (op0) == SSA_NAME)
        retval |= register_edge_assert_for_2 (op0, e, bsi, rhs_code, op0, op1,
					      invert);
      if (TREE_CODE (op1) == SSA_NAME)
        retval |= register_edge_assert_for_2 (op1, e, bsi, rhs_code, op0, op1,
					      invert);
    }
  else if ((code == NE_EXPR
	    && (gimple_assign_rhs_code (op_def) == TRUTH_AND_EXPR
		|| gimple_assign_rhs_code (op_def) == BIT_AND_EXPR))
	   || (code == EQ_EXPR
	       && (gimple_assign_rhs_code (op_def) == TRUTH_OR_EXPR
		   || gimple_assign_rhs_code (op_def) == BIT_IOR_EXPR)))
    {
      /* Recurse on each operand.  */
      retval |= register_edge_assert_for_1 (gimple_assign_rhs1 (op_def),
					    code, e, bsi);
      retval |= register_edge_assert_for_1 (gimple_assign_rhs2 (op_def),
					    code, e, bsi);
    }
  else if (gimple_assign_rhs_code (op_def) == TRUTH_NOT_EXPR)
    {
      /* Recurse, flipping CODE.  */
      code = invert_tree_comparison (code, false);
      retval |= register_edge_assert_for_1 (gimple_assign_rhs1 (op_def),
					    code, e, bsi);
    }
  else if (gimple_assign_rhs_code (op_def) == SSA_NAME)
    {
      /* Recurse through the copy.  */
      retval |= register_edge_assert_for_1 (gimple_assign_rhs1 (op_def),
					    code, e, bsi);
    }
  else if (CONVERT_EXPR_CODE_P (gimple_assign_rhs_code (op_def)))
    {
      /* Recurse through the type conversion.  */
      retval |= register_edge_assert_for_1 (gimple_assign_rhs1 (op_def),
					    code, e, bsi);
    }

  return retval;
}

/* Try to register an edge assertion for SSA name NAME on edge E for
   the condition COND contributing to the conditional jump pointed to by SI.
   Return true if an assertion for NAME could be registered.  */

static bool
register_edge_assert_for (tree name, edge e, gimple_stmt_iterator si,
			  enum tree_code cond_code, tree cond_op0,
			  tree cond_op1)
{
  tree val;
  enum tree_code comp_code;
  bool retval = false;
  bool is_else_edge = (e->flags & EDGE_FALSE_VALUE) != 0;

  /* Do not attempt to infer anything in names that flow through
     abnormal edges.  */
  if (SSA_NAME_OCCURS_IN_ABNORMAL_PHI (name))
    return false;

  if (!extract_code_and_val_from_cond_with_ops (name, cond_code,
						cond_op0, cond_op1,
						is_else_edge,
						&comp_code, &val))
    return false;

  /* Register ASSERT_EXPRs for name.  */
  retval |= register_edge_assert_for_2 (name, e, si, cond_code, cond_op0,
					cond_op1, is_else_edge);


  /* If COND is effectively an equality test of an SSA_NAME against
     the value zero or one, then we may be able to assert values
     for SSA_NAMEs which flow into COND.  */

  /* In the case of NAME == 1 or NAME != 0, for TRUTH_AND_EXPR defining
     statement of NAME we can assert both operands of the TRUTH_AND_EXPR
     have nonzero value.  */
  if (((comp_code == EQ_EXPR && integer_onep (val))
       || (comp_code == NE_EXPR && integer_zerop (val))))
    {
      GIMPLE_type def_stmt = SSA_NAME_DEF_STMT (name);

      if (is_gimple_assign (def_stmt)
	  && (gimple_assign_rhs_code (def_stmt) == TRUTH_AND_EXPR
	      || gimple_assign_rhs_code (def_stmt) == BIT_AND_EXPR))
	{
	  tree op0 = gimple_assign_rhs1 (def_stmt);
	  tree op1 = gimple_assign_rhs2 (def_stmt);
	  retval |= register_edge_assert_for_1 (op0, NE_EXPR, e, si);
	  retval |= register_edge_assert_for_1 (op1, NE_EXPR, e, si);
	}
    }

  /* In the case of NAME == 0 or NAME != 1, for TRUTH_OR_EXPR defining
     statement of NAME we can assert both operands of the TRUTH_OR_EXPR
     have zero value.  */
  if (((comp_code == EQ_EXPR && integer_zerop (val))
       || (comp_code == NE_EXPR && integer_onep (val))))
    {
      GIMPLE_type def_stmt = SSA_NAME_DEF_STMT (name);

      if (is_gimple_assign (def_stmt)
	  && (gimple_assign_rhs_code (def_stmt) == TRUTH_OR_EXPR
	      /* For BIT_IOR_EXPR only if NAME == 0 both operands have
		 necessarily zero value.  */
	      || (comp_code == EQ_EXPR
		  && (gimple_assign_rhs_code (def_stmt) == BIT_IOR_EXPR))))
	{
	  tree op0 = gimple_assign_rhs1 (def_stmt);
	  tree op1 = gimple_assign_rhs2 (def_stmt);
	  retval |= register_edge_assert_for_1 (op0, EQ_EXPR, e, si);
	  retval |= register_edge_assert_for_1 (op1, EQ_EXPR, e, si);
	}
    }

  return retval;
}


/* Determine whether the outgoing edges of BB should receive an
   ASSERT_EXPR for each of the operands of BB's LAST statement.
   The last statement of BB must be a COND_EXPR.

   If any of the sub-graphs rooted at BB have an interesting use of
   the predicate operands, an assert location node is added to the
   list of assertions for the corresponding operands.  */

static bool
find_conditional_asserts (basic_block bb, GIMPLE_type last)
{
  bool need_assert;
  gimple_stmt_iterator bsi;
  tree op;
  edge_iterator ei;
  edge e;
  ssa_op_iter iter;

  need_assert = false;
  bsi = gsi_for_stmt (last);

  /* Look for uses of the operands in each of the sub-graphs
     rooted at BB.  We need to check each of the outgoing edges
     separately, so that we know what kind of ASSERT_EXPR to
     insert.  */
  FOR_EACH_EDGE (e, ei, bb->succs)
    {
      if (e->dest == bb)
	continue;

      /* Register the necessary assertions for each operand in the
	 conditional predicate.  */
      FOR_EACH_SSA_TREE_OPERAND (op, last, iter, SSA_OP_USE)
	{
	  need_assert |= register_edge_assert_for (op, e, bsi,
						   gimple_cond_code (last),
						   gimple_cond_lhs (last),
						   gimple_cond_rhs (last));
	}
    }

  return need_assert;
}

struct case_info
{
  tree expr;
  basic_block bb;
};

/* Compare two case labels sorting first by the destination bb index
   and then by the case value.  */

static int
compare_case_labels (const void *p1, const void *p2)
{
  const struct case_info *ci1 = (const struct case_info *) p1;
  const struct case_info *ci2 = (const struct case_info *) p2;
  int idx1 = ci1->bb->index;
  int idx2 = ci2->bb->index;

  if (idx1 < idx2)
    return -1;
  else if (idx1 == idx2)
    {
      /* Make sure the default label is first in a group.  */
      if (!CASE_LOW (ci1->expr))
	return -1;
      else if (!CASE_LOW (ci2->expr))
	return 1;
      else
	return tree_int_cst_compare (CASE_LOW (ci1->expr),
				     CASE_LOW (ci2->expr));
    }
  else
    return 1;
}

/* Determine whether the outgoing edges of BB should receive an
   ASSERT_EXPR for each of the operands of BB's LAST statement.
   The last statement of BB must be a SWITCH_EXPR.

   If any of the sub-graphs rooted at BB have an interesting use of
   the predicate operands, an assert location node is added to the
   list of assertions for the corresponding operands.  */

static bool
find_switch_asserts (basic_block bb, GIMPLE_type last)
{
  bool need_assert;
  gimple_stmt_iterator bsi;
  tree op;
  edge e;
  struct case_info *ci;
  size_t n = gimple_switch_num_labels (last);
#if GCC_VERSION >= 4000
  unsigned int idx;
#else
  /* Work around GCC 3.4 bug (PR 37086).  */
  volatile unsigned int idx;
#endif

  need_assert = false;
  bsi = gsi_for_stmt (last);
  op = gimple_switch_index (last);
  if (TREE_CODE (op) != SSA_NAME)
    return false;

  /* Build a vector of case labels sorted by destination label.  */
  ci = XNEWVEC (struct case_info, n);
  for (idx = 0; idx < n; ++idx)
    {
      ci[idx].expr = gimple_switch_label (last, idx);
      ci[idx].bb = label_to_block (CASE_LABEL (ci[idx].expr));
    }
  qsort (ci, n, sizeof (struct case_info), compare_case_labels);

  for (idx = 0; idx < n; ++idx)
    {
      tree min, max;
      tree cl = ci[idx].expr;
      basic_block cbb = ci[idx].bb;

      min = CASE_LOW (cl);
      max = CASE_HIGH (cl);

      /* If there are multiple case labels with the same destination
	 we need to combine them to a single value range for the edge.  */
      if (idx + 1 < n && cbb == ci[idx + 1].bb)
	{
	  /* Skip labels until the last of the group.  */
	  do {
	    ++idx;
	  } while (idx < n && cbb == ci[idx].bb);
	  --idx;

	  /* Pick up the maximum of the case label range.  */
	  if (CASE_HIGH (ci[idx].expr))
	    max = CASE_HIGH (ci[idx].expr);
	  else
	    max = CASE_LOW (ci[idx].expr);
	}

      /* Nothing to do if the range includes the default label until we
	 can register anti-ranges.  */
      if (min == NULL_TREE)
	continue;

      /* Find the edge to register the assert expr on.  */
      e = find_edge (bb, cbb);

      /* Register the necessary assertions for the operand in the
	 SWITCH_EXPR.  */
      need_assert |= register_edge_assert_for (op, e, bsi,
					       max ? GE_EXPR : EQ_EXPR,
					       op,
					       fold_convert (TREE_TYPE (op),
							     min));
      if (max)
	{
	  need_assert |= register_edge_assert_for (op, e, bsi, LE_EXPR,
						   op,
						   fold_convert (TREE_TYPE (op),
								 max));
	}
    }

  XDELETEVEC (ci);
  return need_assert;
}


/* Traverse all the statements in block BB looking for statements that
   may generate useful assertions for the SSA names in their operand.
   If a statement produces a useful assertion A for name N_i, then the
   list of assertions already generated for N_i is scanned to
   determine if A is actually needed.

   If N_i already had the assertion A at a location dominating the
   current location, then nothing needs to be done.  Otherwise, the
   new location for A is recorded instead.

   1- For every statement S in BB, all the variables used by S are
      added to bitmap FOUND_IN_SUBGRAPH.

   2- If statement S uses an operand N in a way that exposes a known
      value range for N, then if N was not already generated by an
      ASSERT_EXPR, create a new assert location for N.  For instance,
      if N is a pointer and the statement dereferences it, we can
      assume that N is not NULL.

   3- COND_EXPRs are a special case of #2.  We can derive range
      information from the predicate but need to insert different
      ASSERT_EXPRs for each of the sub-graphs rooted at the
      conditional block.  If the last statement of BB is a conditional
      expression of the form 'X op Y', then

      a) Remove X and Y from the set FOUND_IN_SUBGRAPH.

      b) If the conditional is the only entry point to the sub-graph
	 corresponding to the THEN_CLAUSE, recurse into it.  On
	 return, if X and/or Y are marked in FOUND_IN_SUBGRAPH, then
	 an ASSERT_EXPR is added for the corresponding variable.

      c) Repeat step (b) on the ELSE_CLAUSE.

      d) Mark X and Y in FOUND_IN_SUBGRAPH.

      For instance,

	    if (a == 9)
	      b = a;
	    else
	      b = c + 1;

      In this case, an assertion on the THEN clause is useful to
      determine that 'a' is always 9 on that edge.  However, an assertion
      on the ELSE clause would be unnecessary.

   4- If BB does not end in a conditional expression, then we recurse
      into BB's dominator children.

   At the end of the recursive traversal, every SSA name will have a
   list of locations where ASSERT_EXPRs should be added.  When a new
   location for name N is found, it is registered by calling
   register_new_assert_for.  That function keeps track of all the
   registered assertions to prevent adding unnecessary assertions.
   For instance, if a pointer P_4 is dereferenced more than once in a
   dominator tree, only the location dominating all the dereference of
   P_4 will receive an ASSERT_EXPR.

   If this function returns true, then it means that there are names
   for which we need to generate ASSERT_EXPRs.  Those assertions are
   inserted by process_assert_insertions.  */

static bool
find_assert_locations_1 (basic_block bb, sbitmap live)
{
  gimple_stmt_iterator si;
  GIMPLE_type last;
  GIMPLE_type phi;
  bool need_assert;

  need_assert = false;
  last = last_stmt (bb);

  /* If BB's last statement is a conditional statement involving integer
     operands, determine if we need to add ASSERT_EXPRs.  */
  if (last
      && gimple_code (last) == GIMPLE_COND
      && !fp_predicate (last)
      && !ZERO_SSA_OPERANDS (last, SSA_OP_USE))
    need_assert |= find_conditional_asserts (bb, last);

  /* If BB's last statement is a switch statement involving integer
     operands, determine if we need to add ASSERT_EXPRs.  */
  if (last
      && gimple_code (last) == GIMPLE_SWITCH
      && !ZERO_SSA_OPERANDS (last, SSA_OP_USE))
    need_assert |= find_switch_asserts (bb, last);

  /* Traverse all the statements in BB marking used names and looking
     for statements that may infer assertions for their used operands.  */
  for (si = gsi_start_bb (bb); !gsi_end_p (si); gsi_next (&si))
    {
      GIMPLE_type stmt;
      tree op;
      ssa_op_iter i;

      stmt = gsi_stmt (si);

      if (is_gimple_debug (stmt))
	continue;

      /* See if we can derive an assertion for any of STMT's operands.  */
      FOR_EACH_SSA_TREE_OPERAND (op, stmt, i, SSA_OP_USE)
	{
	  tree value;
	  enum tree_code comp_code;

	  /* Mark OP in our live bitmap.  */
	  SET_BIT (live, SSA_NAME_VERSION (op));

	  /* If OP is used in such a way that we can infer a value
	     range for it, and we don't find a previous assertion for
	     it, create a new assertion location node for OP.  */
	  if (infer_value_range (stmt, op, &comp_code, &value))
	    {
	      /* If we are able to infer a nonzero value range for OP,
		 then walk backwards through the use-def chain to see if OP
		 was set via a typecast.

		 If so, then we can also infer a nonzero value range
		 for the operand of the NOP_EXPR.  */
	      if (comp_code == NE_EXPR && integer_zerop (value))
		{
		  tree t = op;
        GIMPLE_type def_stmt = SSA_NAME_DEF_STMT (t);

		  while (is_gimple_assign (def_stmt)
			 && gimple_assign_rhs_code (def_stmt)  == NOP_EXPR
			 && TREE_CODE
			     (gimple_assign_rhs1 (def_stmt)) == SSA_NAME
			 && POINTER_TYPE_P
			     (TREE_TYPE (gimple_assign_rhs1 (def_stmt))))
		    {
		      t = gimple_assign_rhs1 (def_stmt);
		      def_stmt = SSA_NAME_DEF_STMT (t);

		      /* Note we want to register the assert for the
			 operand of the NOP_EXPR after SI, not after the
			 conversion.  */
		      if (! has_single_use (t))
			{
			  register_new_assert_for (t, t, comp_code, value,
						   bb, NULL, si);
			  need_assert = true;
			}
		    }
		}

	      /* If OP is used only once, namely in this STMT, don't
		 bother creating an ASSERT_EXPR for it.  Such an
		 ASSERT_EXPR would do nothing but increase compile time.  */
	      if (!has_single_use (op))
		{
		  register_new_assert_for (op, op, comp_code, value,
					   bb, NULL, si);
		  need_assert = true;
		}
	    }
	}
    }

  /* Traverse all PHI nodes in BB marking used operands.  */
  for (si = gsi_start_phis (bb); !gsi_end_p(si); gsi_next (&si))
    {
      use_operand_p arg_p;
      ssa_op_iter i;
      phi = gsi_stmt (si);

      FOR_EACH_PHI_ARG (arg_p, phi, i, SSA_OP_USE)
	{
	  tree arg = USE_FROM_PTR (arg_p);
	  if (TREE_CODE (arg) == SSA_NAME)
	    SET_BIT (live, SSA_NAME_VERSION (arg));
	}
    }

  return need_assert;
}

/* Do an RPO walk over the function computing SSA name liveness
   on-the-fly and deciding on assert expressions to insert.
   Returns true if there are assert expressions to be inserted.  */

static bool
find_assert_locations (void)
{
  int *rpo = XCNEWVEC (int, last_basic_block + NUM_FIXED_BLOCKS);
  int *bb_rpo = XCNEWVEC (int, last_basic_block + NUM_FIXED_BLOCKS);
  int *last_rpo = XCNEWVEC (int, last_basic_block + NUM_FIXED_BLOCKS);
  int rpo_cnt, i;
  bool need_asserts;

  live = XCNEWVEC (sbitmap, last_basic_block + NUM_FIXED_BLOCKS);
  rpo_cnt = pre_and_rev_post_order_compute (NULL, rpo, false);
  for (i = 0; i < rpo_cnt; ++i)
    bb_rpo[rpo[i]] = i;

  need_asserts = false;
  for (i = rpo_cnt-1; i >= 0; --i)
    {
      basic_block bb = BASIC_BLOCK (rpo[i]);
      edge e;
      edge_iterator ei;

      if (!live[rpo[i]])
	{
	  live[rpo[i]] = sbitmap_alloc (num_ssa_names);
	  sbitmap_zero (live[rpo[i]]);
	}

      /* Process BB and update the live information with uses in
         this block.  */
      need_asserts |= find_assert_locations_1 (bb, live[rpo[i]]);

      /* Merge liveness into the predecessor blocks and free it.  */
      if (!sbitmap_empty_p (live[rpo[i]]))
	{
	  int pred_rpo = i;
	  FOR_EACH_EDGE (e, ei, bb->preds)
	    {
	      int pred = e->src->index;
	      if (e->flags & EDGE_DFS_BACK)
		continue;

	      if (!live[pred])
		{
		  live[pred] = sbitmap_alloc (num_ssa_names);
		  sbitmap_zero (live[pred]);
		}
	      sbitmap_a_or_b (live[pred], live[pred], live[rpo[i]]);

	      if (bb_rpo[pred] < pred_rpo)
		pred_rpo = bb_rpo[pred];
	    }

	  /* Record the RPO number of the last visited block that needs
	     live information from this block.  */
	  last_rpo[rpo[i]] = pred_rpo;
	}
      else
	{
	  sbitmap_free (live[rpo[i]]);
	  live[rpo[i]] = NULL;
	}

      /* We can free all successors live bitmaps if all their
         predecessors have been visited already.  */
      FOR_EACH_EDGE (e, ei, bb->succs)
	if (last_rpo[e->dest->index] == i
	    && live[e->dest->index])
	  {
	    sbitmap_free (live[e->dest->index]);
	    live[e->dest->index] = NULL;
	  }
    }

  XDELETEVEC (rpo);
  XDELETEVEC (bb_rpo);
  XDELETEVEC (last_rpo);
  for (i = 0; i < last_basic_block + NUM_FIXED_BLOCKS; ++i)
    if (live[i])
      sbitmap_free (live[i]);
  XDELETEVEC (live);

  return need_asserts;
}

/* Create an ASSERT_EXPR for NAME and insert it in the location
   indicated by LOC.  Return true if we made any edge insertions.  */

static bool
process_assert_insertions_for (tree name, assert_locus_t loc)
{
  /* Build the comparison expression NAME_i COMP_CODE VAL.  */
  GIMPLE_type stmt;
  tree cond;
  GIMPLE_type assert_stmt;
  edge_iterator ei;
  edge e;

  /* If we have X <=> X do not insert an assert expr for that.  */
  if (loc->expr == loc->val)
    return false;

  cond = build2 (loc->comp_code, boolean_type_node, loc->expr, loc->val);
  assert_stmt = build_assert_expr_for (cond, name);
  if (loc->e)
    {
      /* We have been asked to insert the assertion on an edge.  This
	 is used only by COND_EXPR and SWITCH_EXPR assertions.  */
      gcc_checking_assert (gimple_code (gsi_stmt (loc->si)) == GIMPLE_COND
			   || (gimple_code (gsi_stmt (loc->si))
			       == GIMPLE_SWITCH));

      gsi_insert_on_edge (loc->e, assert_stmt);
      return true;
    }

  /* Otherwise, we can insert right after LOC->SI iff the
     statement must not be the last statement in the block.  */
  stmt = gsi_stmt (loc->si);
  if (!stmt_ends_bb_p (stmt))
    {
      gsi_insert_after (&loc->si, assert_stmt, GSI_SAME_STMT);
      return false;
    }

  /* If STMT must be the last statement in BB, we can only insert new
     assertions on the non-abnormal edge out of BB.  Note that since
     STMT is not control flow, there may only be one non-abnormal edge
     out of BB.  */
  FOR_EACH_EDGE (e, ei, loc->bb->succs)
    if (!(e->flags & EDGE_ABNORMAL))
      {
	gsi_insert_on_edge (e, assert_stmt);
	return true;
      }

  gcc_unreachable ();
}


/* Process all the insertions registered for every name N_i registered
   in NEED_ASSERT_FOR.  The list of assertions to be inserted are
   found in ASSERTS_FOR[i].  */

static void
process_assert_insertions (void)
{
  unsigned i;
  bitmap_iterator bi;
  bool update_edges_p = false;
  int num_asserts = 0;

  if (dump_file && (dump_flags & TDF_DETAILS))
    dump_all_asserts (dump_file);

  EXECUTE_IF_SET_IN_BITMAP (need_assert_for, 0, i, bi)
    {
      assert_locus_t loc = asserts_for[i];
      gcc_assert (loc);

      while (loc)
	{
	  assert_locus_t next = loc->next;
	  update_edges_p |= process_assert_insertions_for (ssa_name (i), loc);
	  free (loc);
	  loc = next;
	  num_asserts++;
	}
    }

  if (update_edges_p)
    gsi_commit_edge_inserts ();

  statistics_counter_event (cfun, "Number of ASSERT_EXPR expressions inserted",
			    num_asserts);
}


/* Traverse the flowgraph looking for conditional jumps to insert range
   expressions.  These range expressions are meant to provide information
   to optimizations that need to reason in terms of value ranges.  They
   will not be expanded into RTL.  For instance, given:

   x = ...
   y = ...
   if (x < y)
     y = x - 2;
   else
     x = y + 3;

   this pass will transform the code into:

   x = ...
   y = ...
   if (x < y)
    {
      x = ASSERT_EXPR <x, x < y>
      y = x - 2
    }
   else
    {
      y = ASSERT_EXPR <y, x <= y>
      x = y + 3
    }

   The idea is that once copy and constant propagation have run, other
   optimizations will be able to determine what ranges of values can 'x'
   take in different paths of the code, simply by checking the reaching
   definition of 'x'.  */

static void
insert_range_assertions (void)
{
  need_assert_for = BITMAP_ALLOC (NULL);
  asserts_for = XCNEWVEC (assert_locus_t, num_ssa_names);

  calculate_dominance_info (CDI_DOMINATORS);

  if (find_assert_locations ())
    {
      process_assert_insertions ();
      update_ssa (TODO_update_ssa_no_phi);
    }

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "\nSSA form after inserting ASSERT_EXPRs\n");
      dump_function_to_file (current_function_decl, dump_file, dump_flags);
    }

  free (asserts_for);
  BITMAP_FREE (need_assert_for);
}

/* Checks one ARRAY_REF in REF, located at LOCUS. Ignores flexible arrays
   and "struct" hacks. If VRP can determine that the
   array subscript is a constant, check if it is outside valid
   range. If the array subscript is a RANGE, warn if it is
   non-overlapping with valid range.
   IGNORE_OFF_BY_ONE is true if the ARRAY_REF is inside a ADDR_EXPR.  */

static void
check_array_ref (location_t location, tree ref, bool ignore_off_by_one)
{
  value_range_t* vr = NULL;
  tree low_sub, up_sub;
  tree low_bound, up_bound, up_bound_p1;
  tree base;

  if (TREE_NO_WARNING (ref))
    return;

  low_sub = up_sub = TREE_OPERAND (ref, 1);
  up_bound = array_ref_up_bound (ref);

  /* Can not check flexible arrays.  */
  if (!up_bound
      || TREE_CODE (up_bound) != INTEGER_CST)
    return;

  /* Accesses to trailing arrays via pointers may access storage
     beyond the types array bounds.  */
  base = get_base_address (ref);
  if (base && TREE_CODE (base) == MEM_REF)
    {
      tree cref, next = NULL_TREE;

      if (TREE_CODE (TREE_OPERAND (ref, 0)) != COMPONENT_REF)
	return;

      cref = TREE_OPERAND (ref, 0);
      if (TREE_CODE (TREE_TYPE (TREE_OPERAND (cref, 0))) == RECORD_TYPE)
	for (next = DECL_CHAIN (TREE_OPERAND (cref, 1));
	     next && TREE_CODE (next) != FIELD_DECL;
	     next = DECL_CHAIN (next))
	  ;

      /* If this is the last field in a struct type or a field in a
	 union type do not warn.  */
      if (!next)
	return;
    }

  low_bound = array_ref_low_bound (ref);
  up_bound_p1 = int_const_binop (PLUS_EXPR, up_bound, integer_one_node, 0);

  if (TREE_CODE (low_sub) == SSA_NAME)
    {
      vr = get_value_range (low_sub);
      if (vr->type == VR_RANGE || vr->type == VR_ANTI_RANGE)
        {
          low_sub = vr->type == VR_RANGE ? vr->max : vr->min;
          up_sub = vr->type == VR_RANGE ? vr->min : vr->max;
        }
    }

  if (vr && vr->type == VR_ANTI_RANGE)
    {
      if (TREE_CODE (up_sub) == INTEGER_CST
          && tree_int_cst_lt (up_bound, up_sub)
          && TREE_CODE (low_sub) == INTEGER_CST
          && tree_int_cst_lt (low_sub, low_bound))
        {
          warning_at (location, OPT_Warray_bounds,
		      "array subscript is outside array bounds");
          TREE_NO_WARNING (ref) = 1;
        }
    }
  else if (TREE_CODE (up_sub) == INTEGER_CST
	   && (ignore_off_by_one
	       ? (tree_int_cst_lt (up_bound, up_sub)
		  && !tree_int_cst_equal (up_bound_p1, up_sub))
	       : (tree_int_cst_lt (up_bound, up_sub)
		  || tree_int_cst_equal (up_bound_p1, up_sub))))
    {
      warning_at (location, OPT_Warray_bounds,
		  "array subscript is above array bounds");
      TREE_NO_WARNING (ref) = 1;
    }
  else if (TREE_CODE (low_sub) == INTEGER_CST
           && tree_int_cst_lt (low_sub, low_bound))
    {
      warning_at (location, OPT_Warray_bounds,
		  "array subscript is below array bounds");
      TREE_NO_WARNING (ref) = 1;
    }
}

/* Searches if the expr T, located at LOCATION computes
   address of an ARRAY_REF, and call check_array_ref on it.  */

static void
search_for_addr_array (tree t, location_t location)
{
  while (TREE_CODE (t) == SSA_NAME)
    {
      GIMPLE_type g = SSA_NAME_DEF_STMT (t);

      if (gimple_code (g) != GIMPLE_ASSIGN)
	return;

      if (get_gimple_rhs_class (gimple_assign_rhs_code (g))
	  != GIMPLE_SINGLE_RHS)
	return;

      t = gimple_assign_rhs1 (g);
    }


  /* We are only interested in addresses of ARRAY_REF's.  */
  if (TREE_CODE (t) != ADDR_EXPR)
    return;

  /* Check each ARRAY_REFs in the reference chain. */
  do
    {
      if (TREE_CODE (t) == ARRAY_REF)
	check_array_ref (location, t, true /*ignore_off_by_one*/);

      t = TREE_OPERAND (t, 0);
    }
  while (handled_component_p (t));

  if (TREE_CODE (t) == MEM_REF
      && TREE_CODE (TREE_OPERAND (t, 0)) == ADDR_EXPR
      && !TREE_NO_WARNING (t))
    {
      tree tem = TREE_OPERAND (TREE_OPERAND (t, 0), 0);
      tree low_bound, up_bound, el_sz;
      double_int idx;
      if (TREE_CODE (TREE_TYPE (tem)) != ARRAY_TYPE
	  || TREE_CODE (TREE_TYPE (TREE_TYPE (tem))) == ARRAY_TYPE
	  || !TYPE_DOMAIN (TREE_TYPE (tem)))
	return;

      low_bound = TYPE_MIN_VALUE (TYPE_DOMAIN (TREE_TYPE (tem)));
      up_bound = TYPE_MAX_VALUE (TYPE_DOMAIN (TREE_TYPE (tem)));
      el_sz = TYPE_SIZE_UNIT (TREE_TYPE (TREE_TYPE (tem)));
      if (!low_bound
	  || TREE_CODE (low_bound) != INTEGER_CST
	  || !up_bound
	  || TREE_CODE (up_bound) != INTEGER_CST
	  || !el_sz
	  || TREE_CODE (el_sz) != INTEGER_CST)
	return;

      idx = mem_ref_offset (t);
      idx = double_int_sdiv (idx, tree_to_double_int (el_sz), TRUNC_DIV_EXPR);
      if (double_int_scmp (idx, double_int_zero) < 0)
	{
	  warning_at (location, OPT_Warray_bounds,
		      "array subscript is below array bounds");
	  TREE_NO_WARNING (t) = 1;
	}
      else if (double_int_scmp (idx,
				double_int_add
				  (double_int_add
				    (tree_to_double_int (up_bound),
				     double_int_neg
				       (tree_to_double_int (low_bound))),
				    double_int_one)) > 0)
	{
	  warning_at (location, OPT_Warray_bounds,
		      "array subscript is above array bounds");
	  TREE_NO_WARNING (t) = 1;
	}
    }
}

/* walk_tree() callback that checks if *TP is
   an ARRAY_REF inside an ADDR_EXPR (in which an array
   subscript one outside the valid range is allowed). Call
   check_array_ref for each ARRAY_REF found. The location is
   passed in DATA.  */

static tree
check_array_bounds (tree *tp, int *walk_subtree, void *data)
{
  tree t = *tp;
  struct walk_stmt_info *wi = (struct walk_stmt_info *) data;
  location_t location;

  if (EXPR_HAS_LOCATION (t))
    location = EXPR_LOCATION (t);
  else
    {
      location_t *locp = (location_t *) wi->info;
      location = *locp;
    }

  *walk_subtree = TRUE;

  if (TREE_CODE (t) == ARRAY_REF)
    check_array_ref (location, t, false /*ignore_off_by_one*/);

  if (TREE_CODE (t) == MEM_REF
      || (TREE_CODE (t) == RETURN_EXPR && TREE_OPERAND (t, 0)))
    search_for_addr_array (TREE_OPERAND (t, 0), location);

  if (TREE_CODE (t) == ADDR_EXPR)
    *walk_subtree = FALSE;

  return NULL_TREE;
}

/* Walk over all statements of all reachable BBs and call check_array_bounds
   on them.  */

static void
check_all_array_refs (void)
{
  basic_block bb;
  gimple_stmt_iterator si;

  FOR_EACH_BB (bb)
    {
      edge_iterator ei;
      edge e;
      bool executable = false;

      /* Skip blocks that were found to be unreachable.  */
      FOR_EACH_EDGE (e, ei, bb->preds)
	executable |= !!(e->flags & EDGE_EXECUTABLE);
      if (!executable)
	continue;

      for (si = gsi_start_bb (bb); !gsi_end_p (si); gsi_next (&si))
	{
     GIMPLE_type stmt = gsi_stmt (si);
	  struct walk_stmt_info wi;
	  if (!gimple_has_location (stmt))
	    continue;

	  if (is_gimple_call (stmt))
	    {
	      size_t i;
	      size_t n = gimple_call_num_args (stmt);
	      for (i = 0; i < n; i++)
		{
		  tree arg = gimple_call_arg (stmt, i);
		  search_for_addr_array (arg, gimple_location (stmt));
		}
	    }
	  else
	    {
	      memset (&wi, 0, sizeof (wi));
	      wi.info = CONST_CAST (void *, (const void *)
				    gimple_location_ptr (stmt));

	      walk_gimple_op (gsi_stmt (si),
			      check_array_bounds,
			      &wi);
	    }
	}
    }
}

/* Convert range assertion expressions into the implied copies and
   copy propagate away the copies.  Doing the trivial copy propagation
   here avoids the need to run the full copy propagation pass after
   VRP.

   FIXME, this will eventually lead to copy propagation removing the
   names that had useful range information attached to them.  For
   instance, if we had the assertion N_i = ASSERT_EXPR <N_j, N_j > 3>,
   then N_i will have the range [3, +INF].

   However, by converting the assertion into the implied copy
   operation N_i = N_j, we will then copy-propagate N_j into the uses
   of N_i and lose the range information.  We may want to hold on to
   ASSERT_EXPRs a little while longer as the ranges could be used in
   things like jump threading.

   The problem with keeping ASSERT_EXPRs around is that passes after
   VRP need to handle them appropriately.

   Another approach would be to make the range information a first
   class property of the SSA_NAME so that it can be queried from
   any pass.  This is made somewhat more complex by the need for
   multiple ranges to be associated with one SSA_NAME.  */

static void
remove_range_assertions (void)
{
  basic_block bb;
  gimple_stmt_iterator si;

  /* Note that the BSI iterator bump happens at the bottom of the
     loop and no bump is necessary if we're removing the statement
     referenced by the current BSI.  */
  FOR_EACH_BB (bb)
    for (si = gsi_start_bb (bb); !gsi_end_p (si);)
      {
   GIMPLE_type stmt = gsi_stmt (si);
   GIMPLE_type use_stmt;

	if (is_gimple_assign (stmt)
	    && gimple_assign_rhs_code (stmt) == ASSERT_EXPR)
	  {
	    tree rhs = gimple_assign_rhs1 (stmt);
	    tree var;
	    tree cond = fold (ASSERT_EXPR_COND (rhs));
	    use_operand_p use_p;
	    imm_use_iterator iter;

	    gcc_assert (cond != boolean_false_node);

	    /* Propagate the RHS into every use of the LHS.  */
	    var = ASSERT_EXPR_VAR (rhs);
	    FOR_EACH_IMM_USE_STMT (use_stmt, iter,
				   gimple_assign_lhs (stmt))
	      FOR_EACH_IMM_USE_ON_STMT (use_p, iter)
		{
		  SET_USE (use_p, var);
		  gcc_assert (TREE_CODE (var) == SSA_NAME);
		}

	    /* And finally, remove the copy, it is not needed.  */
	    gsi_remove (&si, true);
	    release_defs (stmt);
	  }
	else
	  gsi_next (&si);
      }
}


/* Return true if STMT is interesting for VRP.  */

static bool
stmt_interesting_for_vrp (GIMPLE_type stmt)
{
  if (gimple_code (stmt) == GIMPLE_PHI
      && is_gimple_reg (gimple_phi_result (stmt))
      && (INTEGRAL_TYPE_P (TREE_TYPE (gimple_phi_result (stmt)))
	  || POINTER_TYPE_P (TREE_TYPE (gimple_phi_result (stmt)))))
    return true;
  else if (is_gimple_assign (stmt) || is_gimple_call (stmt))
    {
      tree lhs = gimple_get_lhs (stmt);

      /* In general, assignments with virtual operands are not useful
	 for deriving ranges, with the obvious exception of calls to
	 builtin functions.  */
      if (lhs && TREE_CODE (lhs) == SSA_NAME
	  && (INTEGRAL_TYPE_P (TREE_TYPE (lhs))
	      || POINTER_TYPE_P (TREE_TYPE (lhs)))
	  && ((is_gimple_call (stmt)
	       && gimple_call_fndecl (stmt) != NULL_TREE
	       && DECL_IS_BUILTIN (gimple_call_fndecl (stmt)))
	      || !gimple_vuse (stmt)))
	return true;
    }
  else if (gimple_code (stmt) == GIMPLE_COND
	   || gimple_code (stmt) == GIMPLE_SWITCH)
    return true;

  return false;
}


/* Initialize local data structures for VRP.  */

static void
vrp_initialize (void)
{
  basic_block bb;

  vr_value = XCNEWVEC (value_range_t *, num_ssa_names);
  vr_phi_edge_counts = XCNEWVEC (int, num_ssa_names);

  FOR_EACH_BB (bb)
    {
      gimple_stmt_iterator si;

      for (si = gsi_start_phis (bb); !gsi_end_p (si); gsi_next (&si))
	{
     GIMPLE_type phi = gsi_stmt (si);
	  if (!stmt_interesting_for_vrp (phi))
	    {
	      tree lhs = PHI_RESULT (phi);
	      set_value_range_to_varying (get_value_range (lhs));
	      prop_set_simulate_again (phi, false);
	    }
	  else
	    prop_set_simulate_again (phi, true);
	}

      for (si = gsi_start_bb (bb); !gsi_end_p (si); gsi_next (&si))
        {
     GIMPLE_type stmt = gsi_stmt (si);

 	  /* If the statement is a control insn, then we do not
 	     want to avoid simulating the statement once.  Failure
 	     to do so means that those edges will never get added.  */
	  if (stmt_ends_bb_p (stmt))
	    prop_set_simulate_again (stmt, true);
	  else if (!stmt_interesting_for_vrp (stmt))
	    {
	      ssa_op_iter i;
	      tree def;
	      FOR_EACH_SSA_TREE_OPERAND (def, stmt, i, SSA_OP_DEF)
		set_value_range_to_varying (get_value_range (def));
	      prop_set_simulate_again (stmt, false);
	    }
	  else
	    prop_set_simulate_again (stmt, true);
	}
    }
}


/* Visit assignment STMT.  If it produces an interesting range, record
   the SSA name in *OUTPUT_P.  */

static enum ssa_prop_result
vrp_visit_assignment_or_call (GIMPLE_type stmt, tree *output_p)
{
  tree def, lhs;
  ssa_op_iter iter;
  enum gimple_code code = gimple_code (stmt);
  lhs = gimple_get_lhs (stmt);

  /* We only keep track of ranges in integral and pointer types.  */
  if (TREE_CODE (lhs) == SSA_NAME
      && ((INTEGRAL_TYPE_P (TREE_TYPE (lhs))
	   /* It is valid to have NULL MIN/MAX values on a type.  See
	      build_range_type.  */
	   && TYPE_MIN_VALUE (TREE_TYPE (lhs))
	   && TYPE_MAX_VALUE (TREE_TYPE (lhs)))
	  || POINTER_TYPE_P (TREE_TYPE (lhs))))
    {
      value_range_t new_vr = { VR_UNDEFINED, NULL_TREE, NULL_TREE, NULL };

      if (code == GIMPLE_CALL)
	extract_range_basic (&new_vr, stmt);
      else
	extract_range_from_assignment (&new_vr, stmt);

      if (update_value_range (lhs, &new_vr))
	{
	  *output_p = lhs;

	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      fprintf (dump_file, "Found new range for ");
	      print_generic_expr (dump_file, lhs, 0);
	      fprintf (dump_file, ": ");
	      dump_value_range (dump_file, &new_vr);
	      fprintf (dump_file, "\n\n");
	    }

	  if (new_vr.type == VR_VARYING)
	    return SSA_PROP_VARYING;

	  return SSA_PROP_INTERESTING;
	}

      return SSA_PROP_NOT_INTERESTING;
    }

  /* Every other statement produces no useful ranges.  */
  FOR_EACH_SSA_TREE_OPERAND (def, stmt, iter, SSA_OP_DEF)
    set_value_range_to_varying (get_value_range (def));

  return SSA_PROP_VARYING;
}

/* Helper that gets the value range of the SSA_NAME with version I
   or a symbolic range containing the SSA_NAME only if the value range
   is varying or undefined.  */

static inline value_range_t
get_vr_for_comparison (int i)
{
  value_range_t vr = *(vr_value[i]);

  /* If name N_i does not have a valid range, use N_i as its own
     range.  This allows us to compare against names that may
     have N_i in their ranges.  */
  if (vr.type == VR_VARYING || vr.type == VR_UNDEFINED)
    {
      vr.type = VR_RANGE;
      vr.min = ssa_name (i);
      vr.max = ssa_name (i);
    }

  return vr;
}

/* Compare all the value ranges for names equivalent to VAR with VAL
   using comparison code COMP.  Return the same value returned by
   compare_range_with_value, including the setting of
   *STRICT_OVERFLOW_P.  */

static tree
compare_name_with_value (enum tree_code comp, tree var, tree val,
			 bool *strict_overflow_p)
{
  bitmap_iterator bi;
  unsigned i;
  bitmap e;
  tree retval, t;
  int used_strict_overflow;
  bool sop;
  value_range_t equiv_vr;

  /* Get the set of equivalences for VAR.  */
  e = get_value_range (var)->equiv;

  /* Start at -1.  Set it to 0 if we do a comparison without relying
     on overflow, or 1 if all comparisons rely on overflow.  */
  used_strict_overflow = -1;

  /* Compare vars' value range with val.  */
  equiv_vr = get_vr_for_comparison (SSA_NAME_VERSION (var));
  sop = false;
  retval = compare_range_with_value (comp, &equiv_vr, val, &sop);
  if (retval)
    used_strict_overflow = sop ? 1 : 0;

  /* If the equiv set is empty we have done all work we need to do.  */
  if (e == NULL)
    {
      if (retval
	  && used_strict_overflow > 0)
	*strict_overflow_p = true;
      return retval;
    }

  EXECUTE_IF_SET_IN_BITMAP (e, 0, i, bi)
    {
      equiv_vr = get_vr_for_comparison (i);
      sop = false;
      t = compare_range_with_value (comp, &equiv_vr, val, &sop);
      if (t)
	{
	  /* If we get different answers from different members
	     of the equivalence set this check must be in a dead
	     code region.  Folding it to a trap representation
	     would be correct here.  For now just return don't-know.  */
	  if (retval != NULL
	      && t != retval)
	    {
	      retval = NULL_TREE;
	      break;
	    }
	  retval = t;

	  if (!sop)
	    used_strict_overflow = 0;
	  else if (used_strict_overflow < 0)
	    used_strict_overflow = 1;
	}
    }

  if (retval
      && used_strict_overflow > 0)
    *strict_overflow_p = true;

  return retval;
}


/* Given a comparison code COMP and names N1 and N2, compare all the
   ranges equivalent to N1 against all the ranges equivalent to N2
   to determine the value of N1 COMP N2.  Return the same value
   returned by compare_ranges.  Set *STRICT_OVERFLOW_P to indicate
   whether we relied on an overflow infinity in the comparison.  */


static tree
compare_names (enum tree_code comp, tree n1, tree n2,
	       bool *strict_overflow_p)
{
  tree t, retval;
  bitmap e1, e2;
  bitmap_iterator bi1, bi2;
  unsigned i1, i2;
  int used_strict_overflow;
  static bitmap_obstack *s_obstack = NULL;
  static bitmap s_e1 = NULL, s_e2 = NULL;

  /* Compare the ranges of every name equivalent to N1 against the
     ranges of every name equivalent to N2.  */
  e1 = get_value_range (n1)->equiv;
  e2 = get_value_range (n2)->equiv;

  /* Use the fake bitmaps if e1 or e2 are not available.  */
  if (s_obstack == NULL)
    {
      s_obstack = XNEW (bitmap_obstack);
      bitmap_obstack_initialize (s_obstack);
      s_e1 = BITMAP_ALLOC (s_obstack);
      s_e2 = BITMAP_ALLOC (s_obstack);
    }
  if (e1 == NULL)
    e1 = s_e1;
  if (e2 == NULL)
    e2 = s_e2;

  /* Add N1 and N2 to their own set of equivalences to avoid
     duplicating the body of the loop just to check N1 and N2
     ranges.  */
  bitmap_set_bit (e1, SSA_NAME_VERSION (n1));
  bitmap_set_bit (e2, SSA_NAME_VERSION (n2));

  /* If the equivalence sets have a common intersection, then the two
     names can be compared without checking their ranges.  */
  if (bitmap_intersect_p (e1, e2))
    {
      bitmap_clear_bit (e1, SSA_NAME_VERSION (n1));
      bitmap_clear_bit (e2, SSA_NAME_VERSION (n2));

      return (comp == EQ_EXPR || comp == GE_EXPR || comp == LE_EXPR)
	     ? boolean_true_node
	     : boolean_false_node;
    }

  /* Start at -1.  Set it to 0 if we do a comparison without relying
     on overflow, or 1 if all comparisons rely on overflow.  */
  used_strict_overflow = -1;

  /* Otherwise, compare all the equivalent ranges.  First, add N1 and
     N2 to their own set of equivalences to avoid duplicating the body
     of the loop just to check N1 and N2 ranges.  */
  EXECUTE_IF_SET_IN_BITMAP (e1, 0, i1, bi1)
    {
      value_range_t vr1 = get_vr_for_comparison (i1);

      t = retval = NULL_TREE;
      EXECUTE_IF_SET_IN_BITMAP (e2, 0, i2, bi2)
	{
	  bool sop = false;

	  value_range_t vr2 = get_vr_for_comparison (i2);

	  t = compare_ranges (comp, &vr1, &vr2, &sop);
	  if (t)
	    {
	      /* If we get different answers from different members
		 of the equivalence set this check must be in a dead
		 code region.  Folding it to a trap representation
		 would be correct here.  For now just return don't-know.  */
	      if (retval != NULL
		  && t != retval)
		{
		  bitmap_clear_bit (e1, SSA_NAME_VERSION (n1));
		  bitmap_clear_bit (e2, SSA_NAME_VERSION (n2));
		  return NULL_TREE;
		}
	      retval = t;

	      if (!sop)
		used_strict_overflow = 0;
	      else if (used_strict_overflow < 0)
		used_strict_overflow = 1;
	    }
	}

      if (retval)
	{
	  bitmap_clear_bit (e1, SSA_NAME_VERSION (n1));
	  bitmap_clear_bit (e2, SSA_NAME_VERSION (n2));
	  if (used_strict_overflow > 0)
	    *strict_overflow_p = true;
	  return retval;
	}
    }

  /* None of the equivalent ranges are useful in computing this
     comparison.  */
  bitmap_clear_bit (e1, SSA_NAME_VERSION (n1));
  bitmap_clear_bit (e2, SSA_NAME_VERSION (n2));
  return NULL_TREE;
}

/* Helper function for vrp_evaluate_conditional_warnv.  */

static tree
vrp_evaluate_conditional_warnv_with_ops_using_ranges (enum tree_code code,
						      tree op0, tree op1,
						      bool * strict_overflow_p)
{
  value_range_t *vr0, *vr1;

  vr0 = (TREE_CODE (op0) == SSA_NAME) ? get_value_range (op0) : NULL;
  vr1 = (TREE_CODE (op1) == SSA_NAME) ? get_value_range (op1) : NULL;

  if (vr0 && vr1)
    return compare_ranges (code, vr0, vr1, strict_overflow_p);
  else if (vr0 && vr1 == NULL)
    return compare_range_with_value (code, vr0, op1, strict_overflow_p);
  else if (vr0 == NULL && vr1)
    return (compare_range_with_value
	    (swap_tree_comparison (code), vr1, op0, strict_overflow_p));
  return NULL;
}

/* Helper function for vrp_evaluate_conditional_warnv. */

static tree
vrp_evaluate_conditional_warnv_with_ops (enum tree_code code, tree op0,
					 tree op1, bool use_equiv_p,
					 bool *strict_overflow_p, bool *only_ranges)
{
  tree ret;
  if (only_ranges)
    *only_ranges = true;

  /* We only deal with integral and pointer types.  */
  if (!INTEGRAL_TYPE_P (TREE_TYPE (op0))
      && !POINTER_TYPE_P (TREE_TYPE (op0)))
    return NULL_TREE;

  if (use_equiv_p)
    {
      if (only_ranges
          && (ret = vrp_evaluate_conditional_warnv_with_ops_using_ranges
	              (code, op0, op1, strict_overflow_p)))
	return ret;
      *only_ranges = false;
      if (TREE_CODE (op0) == SSA_NAME && TREE_CODE (op1) == SSA_NAME)
	return compare_names (code, op0, op1, strict_overflow_p);
      else if (TREE_CODE (op0) == SSA_NAME)
	return compare_name_with_value (code, op0, op1, strict_overflow_p);
      else if (TREE_CODE (op1) == SSA_NAME)
	return (compare_name_with_value
		(swap_tree_comparison (code), op1, op0, strict_overflow_p));
    }
  else
    return vrp_evaluate_conditional_warnv_with_ops_using_ranges (code, op0, op1,
								 strict_overflow_p);
  return NULL_TREE;
}

/* Given (CODE OP0 OP1) within STMT, try to simplify it based on value range
   information.  Return NULL if the conditional can not be evaluated.
   The ranges of all the names equivalent with the operands in COND
   will be used when trying to compute the value.  If the result is
   based on undefined signed overflow, issue a warning if
   appropriate.  */

static tree
vrp_evaluate_conditional (enum tree_code code, tree op0, tree op1, GIMPLE_type stmt)
{
  bool sop;
  tree ret;
  bool only_ranges;

  /* Some passes and foldings leak constants with overflow flag set
     into the IL.  Avoid doing wrong things with these and bail out.  */
  if ((TREE_CODE (op0) == INTEGER_CST
       && TREE_OVERFLOW (op0))
      || (TREE_CODE (op1) == INTEGER_CST
	  && TREE_OVERFLOW (op1)))
    return NULL_TREE;

  sop = false;
  ret = vrp_evaluate_conditional_warnv_with_ops (code, op0, op1, true, &sop,
  						 &only_ranges);

  if (ret && sop)
    {
      enum warn_strict_overflow_code wc;
      const char* warnmsg;

      if (is_gimple_min_invariant (ret))
	{
	  wc = WARN_STRICT_OVERFLOW_CONDITIONAL;
	  warnmsg = G_("assuming signed overflow does not occur when "
		       "simplifying conditional to constant");
	}
      else
	{
	  wc = WARN_STRICT_OVERFLOW_COMPARISON;
	  warnmsg = G_("assuming signed overflow does not occur when "
		       "simplifying conditional");
	}

      if (issue_strict_overflow_warning (wc))
	{
	  location_t location;

	  if (!gimple_has_location (stmt))
	    location = input_location;
	  else
	    location = gimple_location (stmt);
	  warning_at (location, OPT_Wstrict_overflow, "%s", warnmsg);
	}
    }

  if (warn_type_limits
      && ret && only_ranges
      && TREE_CODE_CLASS (code) == tcc_comparison
      && TREE_CODE (op0) == SSA_NAME)
    {
      /* If the comparison is being folded and the operand on the LHS
	 is being compared against a constant value that is outside of
	 the natural range of OP0's type, then the predicate will
	 always fold regardless of the value of OP0.  If -Wtype-limits
	 was specified, emit a warning.  */
      tree type = TREE_TYPE (op0);
      value_range_t *vr0 = get_value_range (op0);

      if (vr0->type != VR_VARYING
	  && INTEGRAL_TYPE_P (type)
	  && vrp_val_is_min (vr0->min)
	  && vrp_val_is_max (vr0->max)
	  && is_gimple_min_invariant (op1))
	{
	  location_t location;

	  if (!gimple_has_location (stmt))
	    location = input_location;
	  else
	    location = gimple_location (stmt);

	  warning_at (location, OPT_Wtype_limits,
		      integer_zerop (ret)
		      ? G_("comparison always false "
                           "due to limited range of data type")
		      : G_("comparison always true "
                           "due to limited range of data type"));
	}
    }

  return ret;
}


/* Visit conditional statement STMT.  If we can determine which edge
   will be taken out of STMT's basic block, record it in
   *TAKEN_EDGE_P and return SSA_PROP_INTERESTING.  Otherwise, return
   SSA_PROP_VARYING.  */

static enum ssa_prop_result
vrp_visit_cond_stmt (GIMPLE_type stmt, edge *taken_edge_p)
{
  tree val;
  bool sop;

  *taken_edge_p = NULL;

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      tree use;
      ssa_op_iter i;

      fprintf (dump_file, "\nVisiting conditional with predicate: ");
      print_gimple_stmt (dump_file, stmt, 0, 0);
      fprintf (dump_file, "\nWith known ranges\n");

      FOR_EACH_SSA_TREE_OPERAND (use, stmt, i, SSA_OP_USE)
	{
	  fprintf (dump_file, "\t");
	  print_generic_expr (dump_file, use, 0);
	  fprintf (dump_file, ": ");
	  dump_value_range (dump_file, vr_value[SSA_NAME_VERSION (use)]);
	}

      fprintf (dump_file, "\n");
    }

  /* Compute the value of the predicate COND by checking the known
     ranges of each of its operands.

     Note that we cannot evaluate all the equivalent ranges here
     because those ranges may not yet be final and with the current
     propagation strategy, we cannot determine when the value ranges
     of the names in the equivalence set have changed.

     For instance, given the following code fragment

        i_5 = PHI <8, i_13>
	...
     	i_14 = ASSERT_EXPR <i_5, i_5 != 0>
	if (i_14 == 1)
	  ...

     Assume that on the first visit to i_14, i_5 has the temporary
     range [8, 8] because the second argument to the PHI function is
     not yet executable.  We derive the range ~[0, 0] for i_14 and the
     equivalence set { i_5 }.  So, when we visit 'if (i_14 == 1)' for
     the first time, since i_14 is equivalent to the range [8, 8], we
     determine that the predicate is always false.

     On the next round of propagation, i_13 is determined to be
     VARYING, which causes i_5 to drop down to VARYING.  So, another
     visit to i_14 is scheduled.  In this second visit, we compute the
     exact same range and equivalence set for i_14, namely ~[0, 0] and
     { i_5 }.  But we did not have the previous range for i_5
     registered, so vrp_visit_assignment thinks that the range for
     i_14 has not changed.  Therefore, the predicate 'if (i_14 == 1)'
     is not visited again, which stops propagation from visiting
     statements in the THEN clause of that if().

     To properly fix this we would need to keep the previous range
     value for the names in the equivalence set.  This way we would've
     discovered that from one visit to the other i_5 changed from
     range [8, 8] to VR_VARYING.

     However, fixing this apparent limitation may not be worth the
     additional checking.  Testing on several code bases (GCC, DLV,
     MICO, TRAMP3D and SPEC2000) showed that doing this results in
     4 more predicates folded in SPEC.  */
  sop = false;

  val = vrp_evaluate_conditional_warnv_with_ops (gimple_cond_code (stmt),
						 gimple_cond_lhs (stmt),
						 gimple_cond_rhs (stmt),
						 false, &sop, NULL);
  if (val)
    {
      if (!sop)
	*taken_edge_p = find_taken_edge (gimple_bb (stmt), val);
      else
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file,
		     "\nIgnoring predicate evaluation because "
		     "it assumes that signed overflow is undefined");
	  val = NULL_TREE;
	}
    }

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "\nPredicate evaluates to: ");
      if (val == NULL_TREE)
	fprintf (dump_file, "DON'T KNOW\n");
      else
	print_generic_stmt (dump_file, val, 0);
    }

  return (*taken_edge_p) ? SSA_PROP_INTERESTING : SSA_PROP_VARYING;
}

/* Searches the case label vector VEC for the index *IDX of the CASE_LABEL
   that includes the value VAL.  The search is restricted to the range
   [START_IDX, n - 1] where n is the size of VEC.

   If there is a CASE_LABEL for VAL, its index is placed in IDX and true is
   returned.

   If there is no CASE_LABEL for VAL and there is one that is larger than VAL,
   it is placed in IDX and false is returned.

   If VAL is larger than any CASE_LABEL, n is placed on IDX and false is
   returned. */

static bool
find_case_label_index (GIMPLE_type stmt, size_t start_idx, tree val, size_t *idx)
{
  size_t n = gimple_switch_num_labels (stmt);
  size_t low, high;

  /* Find case label for minimum of the value range or the next one.
     At each iteration we are searching in [low, high - 1]. */

  for (low = start_idx, high = n; high != low; )
    {
      tree t;
      int cmp;
      /* Note that i != high, so we never ask for n. */
      size_t i = (high + low) / 2;
      t = gimple_switch_label (stmt, i);

      /* Cache the result of comparing CASE_LOW and val.  */
      cmp = tree_int_cst_compare (CASE_LOW (t), val);

      if (cmp == 0)
	{
	  /* Ranges cannot be empty. */
	  *idx = i;
	  return true;
	}
      else if (cmp > 0)
        high = i;
      else
	{
	  low = i + 1;
	  if (CASE_HIGH (t) != NULL
	      && tree_int_cst_compare (CASE_HIGH (t), val) >= 0)
	    {
	      *idx = i;
	      return true;
	    }
        }
    }

  *idx = high;
  return false;
}

/* Searches the case label vector VEC for the range of CASE_LABELs that is used
   for values between MIN and MAX. The first index is placed in MIN_IDX. The
   last index is placed in MAX_IDX. If the range of CASE_LABELs is empty
   then MAX_IDX < MIN_IDX.
   Returns true if the default label is not needed. */

static bool
find_case_label_range (GIMPLE_type stmt, tree min, tree max, size_t *min_idx,
		       size_t *max_idx)
{
  size_t i, j;
  bool min_take_default = !find_case_label_index (stmt, 1, min, &i);
  bool max_take_default = !find_case_label_index (stmt, i, max, &j);

  if (i == j
      && min_take_default
      && max_take_default)
    {
      /* Only the default case label reached.
         Return an empty range. */
      *min_idx = 1;
      *max_idx = 0;
      return false;
    }
  else
    {
      bool take_default = min_take_default || max_take_default;
      tree low, high;
      size_t k;

      if (max_take_default)
	j--;

      /* If the case label range is continuous, we do not need
	 the default case label.  Verify that.  */
      high = CASE_LOW (gimple_switch_label (stmt, i));
      if (CASE_HIGH (gimple_switch_label (stmt, i)))
	high = CASE_HIGH (gimple_switch_label (stmt, i));
      for (k = i + 1; k <= j; ++k)
	{
	  low = CASE_LOW (gimple_switch_label (stmt, k));
	  if (!integer_onep (int_const_binop (MINUS_EXPR, low, high, 0)))
	    {
	      take_default = true;
	      break;
	    }
	  high = low;
	  if (CASE_HIGH (gimple_switch_label (stmt, k)))
	    high = CASE_HIGH (gimple_switch_label (stmt, k));
	}

      *min_idx = i;
      *max_idx = j;
      return !take_default;
    }
}

/* Visit switch statement STMT.  If we can determine which edge
   will be taken out of STMT's basic block, record it in
   *TAKEN_EDGE_P and return SSA_PROP_INTERESTING.  Otherwise, return
   SSA_PROP_VARYING.  */

static enum ssa_prop_result
vrp_visit_switch_stmt (GIMPLE_type stmt, edge *taken_edge_p)
{
  tree op, val;
  value_range_t *vr;
  size_t i = 0, j = 0;
  bool take_default;

  *taken_edge_p = NULL;
  op = gimple_switch_index (stmt);
  if (TREE_CODE (op) != SSA_NAME)
    return SSA_PROP_VARYING;

  vr = get_value_range (op);
  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "\nVisiting switch expression with operand ");
      print_generic_expr (dump_file, op, 0);
      fprintf (dump_file, " with known range ");
      dump_value_range (dump_file, vr);
      fprintf (dump_file, "\n");
    }

  if (vr->type != VR_RANGE
      || symbolic_range_p (vr))
    return SSA_PROP_VARYING;

  /* Find the single edge that is taken from the switch expression.  */
  take_default = !find_case_label_range (stmt, vr->min, vr->max, &i, &j);

  /* Check if the range spans no CASE_LABEL. If so, we only reach the default
     label */
  if (j < i)
    {
      gcc_assert (take_default);
      val = gimple_switch_default_label (stmt);
    }
  else
    {
      /* Check if labels with index i to j and maybe the default label
	 are all reaching the same label.  */

      val = gimple_switch_label (stmt, i);
      if (take_default
	  && CASE_LABEL (gimple_switch_default_label (stmt))
	  != CASE_LABEL (val))
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file, "  not a single destination for this "
		     "range\n");
          return SSA_PROP_VARYING;
	}
      for (++i; i <= j; ++i)
        {
          if (CASE_LABEL (gimple_switch_label (stmt, i)) != CASE_LABEL (val))
	    {
	      if (dump_file && (dump_flags & TDF_DETAILS))
		fprintf (dump_file, "  not a single destination for this "
			 "range\n");
	      return SSA_PROP_VARYING;
	    }
        }
    }

  *taken_edge_p = find_edge (gimple_bb (stmt),
			     label_to_block (CASE_LABEL (val)));

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "  will take edge to ");
      print_generic_stmt (dump_file, CASE_LABEL (val), 0);
    }

  return SSA_PROP_INTERESTING;
}


/* Evaluate statement STMT.  If the statement produces a useful range,
   return SSA_PROP_INTERESTING and record the SSA name with the
   interesting range into *OUTPUT_P.

   If STMT is a conditional branch and we can determine its truth
   value, the taken edge is recorded in *TAKEN_EDGE_P.

   If STMT produces a varying value, return SSA_PROP_VARYING.  */

static enum ssa_prop_result
vrp_visit_stmt (GIMPLE_type stmt, edge *taken_edge_p, tree *output_p)
{
  tree def;
  ssa_op_iter iter;

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "\nVisiting statement:\n");
      print_gimple_stmt (dump_file, stmt, 0, dump_flags);
      fprintf (dump_file, "\n");
    }

  if (!stmt_interesting_for_vrp (stmt))
    gcc_assert (stmt_ends_bb_p (stmt));
  else if (is_gimple_assign (stmt) || is_gimple_call (stmt))
    {
      /* In general, assignments with virtual operands are not useful
	 for deriving ranges, with the obvious exception of calls to
	 builtin functions.  */

      if ((is_gimple_call (stmt)
	   && gimple_call_fndecl (stmt) != NULL_TREE
	   && DECL_IS_BUILTIN (gimple_call_fndecl (stmt)))
	  || !gimple_vuse (stmt))
	return vrp_visit_assignment_or_call (stmt, output_p);
    }
  else if (gimple_code (stmt) == GIMPLE_COND)
    return vrp_visit_cond_stmt (stmt, taken_edge_p);
  else if (gimple_code (stmt) == GIMPLE_SWITCH)
    return vrp_visit_switch_stmt (stmt, taken_edge_p);

  /* All other statements produce nothing of interest for VRP, so mark
     their outputs varying and prevent further simulation.  */
  FOR_EACH_SSA_TREE_OPERAND (def, stmt, iter, SSA_OP_DEF)
    set_value_range_to_varying (get_value_range (def));

  return SSA_PROP_VARYING;
}


/* Meet operation for value ranges.  Given two value ranges VR0 and
   VR1, store in VR0 a range that contains both VR0 and VR1.  This
   may not be the smallest possible such range.  */

static void
vrp_meet (value_range_t *vr0, value_range_t *vr1)
{
  if (vr0->type == VR_UNDEFINED)
    {
      copy_value_range (vr0, vr1);
      return;
    }

  if (vr1->type == VR_UNDEFINED)
    {
      /* Nothing to do.  VR0 already has the resulting range.  */
      return;
    }

  if (vr0->type == VR_VARYING)
    {
      /* Nothing to do.  VR0 already has the resulting range.  */
      return;
    }

  if (vr1->type == VR_VARYING)
    {
      set_value_range_to_varying (vr0);
      return;
    }

  if (vr0->type == VR_RANGE && vr1->type == VR_RANGE)
    {
      int cmp;
      tree min, max;

      /* Compute the convex hull of the ranges.  The lower limit of
         the new range is the minimum of the two ranges.  If they
	 cannot be compared, then give up.  */
      cmp = compare_values (vr0->min, vr1->min);
      if (cmp == 0 || cmp == 1)
        min = vr1->min;
      else if (cmp == -1)
        min = vr0->min;
      else
	goto give_up;

      /* Similarly, the upper limit of the new range is the maximum
         of the two ranges.  If they cannot be compared, then
	 give up.  */
      cmp = compare_values (vr0->max, vr1->max);
      if (cmp == 0 || cmp == -1)
        max = vr1->max;
      else if (cmp == 1)
        max = vr0->max;
      else
	goto give_up;

      /* Check for useless ranges.  */
      if (INTEGRAL_TYPE_P (TREE_TYPE (min))
	  && ((vrp_val_is_min (min) || is_overflow_infinity (min))
	      && (vrp_val_is_max (max) || is_overflow_infinity (max))))
	goto give_up;

      /* The resulting set of equivalences is the intersection of
	 the two sets.  */
      if (vr0->equiv && vr1->equiv && vr0->equiv != vr1->equiv)
        bitmap_and_into (vr0->equiv, vr1->equiv);
      else if (vr0->equiv && !vr1->equiv)
        bitmap_clear (vr0->equiv);

      set_value_range (vr0, vr0->type, min, max, vr0->equiv);
    }
  else if (vr0->type == VR_ANTI_RANGE && vr1->type == VR_ANTI_RANGE)
    {
      /* Two anti-ranges meet only if their complements intersect.
         Only handle the case of identical ranges.  */
      if (compare_values (vr0->min, vr1->min) == 0
	  && compare_values (vr0->max, vr1->max) == 0
	  && compare_values (vr0->min, vr0->max) == 0)
	{
	  /* The resulting set of equivalences is the intersection of
	     the two sets.  */
	  if (vr0->equiv && vr1->equiv && vr0->equiv != vr1->equiv)
	    bitmap_and_into (vr0->equiv, vr1->equiv);
	  else if (vr0->equiv && !vr1->equiv)
	    bitmap_clear (vr0->equiv);
	}
      else
	goto give_up;
    }
  else if (vr0->type == VR_ANTI_RANGE || vr1->type == VR_ANTI_RANGE)
    {
      /* For a numeric range [VAL1, VAL2] and an anti-range ~[VAL3, VAL4],
         only handle the case where the ranges have an empty intersection.
	 The result of the meet operation is the anti-range.  */
      if (!symbolic_range_p (vr0)
	  && !symbolic_range_p (vr1)
	  && !value_ranges_intersect_p (vr0, vr1))
	{
	  /* Copy most of VR1 into VR0.  Don't copy VR1's equivalence
	     set.  We need to compute the intersection of the two
	     equivalence sets.  */
	  if (vr1->type == VR_ANTI_RANGE)
	    set_value_range (vr0, vr1->type, vr1->min, vr1->max, vr0->equiv);

	  /* The resulting set of equivalences is the intersection of
	     the two sets.  */
	  if (vr0->equiv && vr1->equiv && vr0->equiv != vr1->equiv)
	    bitmap_and_into (vr0->equiv, vr1->equiv);
	  else if (vr0->equiv && !vr1->equiv)
	    bitmap_clear (vr0->equiv);
	}
      else
	goto give_up;
    }
  else
    gcc_unreachable ();

  return;

give_up:
  /* Failed to find an efficient meet.  Before giving up and setting
     the result to VARYING, see if we can at least derive a useful
     anti-range.  FIXME, all this nonsense about distinguishing
     anti-ranges from ranges is necessary because of the odd
     semantics of range_includes_zero_p and friends.  */
  if (!symbolic_range_p (vr0)
      && ((vr0->type == VR_RANGE && !range_includes_zero_p (vr0))
	  || (vr0->type == VR_ANTI_RANGE && range_includes_zero_p (vr0)))
      && !symbolic_range_p (vr1)
      && ((vr1->type == VR_RANGE && !range_includes_zero_p (vr1))
	  || (vr1->type == VR_ANTI_RANGE && range_includes_zero_p (vr1))))
    {
      set_value_range_to_nonnull (vr0, TREE_TYPE (vr0->min));

      /* Since this meet operation did not result from the meeting of
	 two equivalent names, VR0 cannot have any equivalences.  */
      if (vr0->equiv)
	bitmap_clear (vr0->equiv);
    }
  else
    set_value_range_to_varying (vr0);
}


/* Visit all arguments for PHI node PHI that flow through executable
   edges.  If a valid value range can be derived from all the incoming
   value ranges, set a new range for the LHS of PHI.  */

static enum ssa_prop_result
vrp_visit_phi_node (GIMPLE_type phi)
{
  size_t i;
  tree lhs = PHI_RESULT (phi);
  value_range_t *lhs_vr = get_value_range (lhs);
  value_range_t vr_result = { VR_UNDEFINED, NULL_TREE, NULL_TREE, NULL };
  int edges, old_edges;
  struct loop *l;

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "\nVisiting PHI node: ");
      print_gimple_stmt (dump_file, phi, 0, dump_flags);
    }

  edges = 0;
  for (i = 0; i < gimple_phi_num_args (phi); i++)
    {
      edge e = gimple_phi_arg_edge (phi, i);

      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file,
	      "\n    Argument #%d (%d -> %d %sexecutable)\n",
	      (int) i, e->src->index, e->dest->index,
	      (e->flags & EDGE_EXECUTABLE) ? "" : "not ");
	}

      if (e->flags & EDGE_EXECUTABLE)
	{
	  tree arg = PHI_ARG_DEF (phi, i);
	  value_range_t vr_arg;

	  ++edges;

	  if (TREE_CODE (arg) == SSA_NAME)
	    {
	      vr_arg = *(get_value_range (arg));
	    }
	  else
	    {
	      if (is_overflow_infinity (arg))
		{
		  arg = copy_node (arg);
		  TREE_OVERFLOW (arg) = 0;
		}

	      vr_arg.type = VR_RANGE;
	      vr_arg.min = arg;
	      vr_arg.max = arg;
	      vr_arg.equiv = NULL;
	    }

	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      fprintf (dump_file, "\t");
	      print_generic_expr (dump_file, arg, dump_flags);
	      fprintf (dump_file, "\n\tValue: ");
	      dump_value_range (dump_file, &vr_arg);
	      fprintf (dump_file, "\n");
	    }

	  vrp_meet (&vr_result, &vr_arg);

	  if (vr_result.type == VR_VARYING)
	    break;
	}
    }

  if (vr_result.type == VR_VARYING)
    goto varying;

  old_edges = vr_phi_edge_counts[SSA_NAME_VERSION (lhs)];
  vr_phi_edge_counts[SSA_NAME_VERSION (lhs)] = edges;

  /* To prevent infinite iterations in the algorithm, derive ranges
     when the new value is slightly bigger or smaller than the
     previous one.  We don't do this if we have seen a new executable
     edge; this helps us avoid an overflow infinity for conditionals
     which are not in a loop.  */
  if (edges > 0
      && gimple_phi_num_args (phi) > 1
      && edges == old_edges)
    {
      int cmp_min = compare_values (lhs_vr->min, vr_result.min);
      int cmp_max = compare_values (lhs_vr->max, vr_result.max);

      /* For non VR_RANGE or for pointers fall back to varying if
	 the range changed.  */
      if ((lhs_vr->type != VR_RANGE || vr_result.type != VR_RANGE
	   || POINTER_TYPE_P (TREE_TYPE (lhs)))
	  && (cmp_min != 0 || cmp_max != 0))
	goto varying;

      /* If the new minimum is smaller or larger than the previous
	 one, go all the way to -INF.  In the first case, to avoid
	 iterating millions of times to reach -INF, and in the
	 other case to avoid infinite bouncing between different
	 minimums.  */
      if (cmp_min > 0 || cmp_min < 0)
	{
	  if (!needs_overflow_infinity (TREE_TYPE (vr_result.min))
	      || !vrp_var_may_overflow (lhs, phi))
	    vr_result.min = TYPE_MIN_VALUE (TREE_TYPE (vr_result.min));
	  else if (supports_overflow_infinity (TREE_TYPE (vr_result.min)))
	    vr_result.min =
		negative_overflow_infinity (TREE_TYPE (vr_result.min));
	}

      /* Similarly, if the new maximum is smaller or larger than
	 the previous one, go all the way to +INF.  */
      if (cmp_max < 0 || cmp_max > 0)
	{
	  if (!needs_overflow_infinity (TREE_TYPE (vr_result.max))
	      || !vrp_var_may_overflow (lhs, phi))
	    vr_result.max = TYPE_MAX_VALUE (TREE_TYPE (vr_result.max));
	  else if (supports_overflow_infinity (TREE_TYPE (vr_result.max)))
	    vr_result.max =
		positive_overflow_infinity (TREE_TYPE (vr_result.max));
	}

      /* If we dropped either bound to +-INF then if this is a loop
	 PHI node SCEV may known more about its value-range.  */
      if ((cmp_min > 0 || cmp_min < 0
	   || cmp_max < 0 || cmp_max > 0)
	  && current_loops
	  && (l = loop_containing_stmt (phi))
	  && l->header == gimple_bb (phi))
	adjust_range_with_scev (&vr_result, l, phi, lhs);

      /* If we will end up with a (-INF, +INF) range, set it to
	 VARYING.  Same if the previous max value was invalid for
	 the type and we end up with vr_result.min > vr_result.max.  */
      if ((vrp_val_is_max (vr_result.max)
	   && vrp_val_is_min (vr_result.min))
	  || compare_values (vr_result.min,
			     vr_result.max) > 0)
	goto varying;
    }

  /* If the new range is different than the previous value, keep
     iterating.  */
  if (update_value_range (lhs, &vr_result))
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file, "Found new range for ");
	  print_generic_expr (dump_file, lhs, 0);
	  fprintf (dump_file, ": ");
	  dump_value_range (dump_file, &vr_result);
	  fprintf (dump_file, "\n\n");
	}

      return SSA_PROP_INTERESTING;
    }

  /* Nothing changed, don't add outgoing edges.  */
  return SSA_PROP_NOT_INTERESTING;

  /* No match found.  Set the LHS to VARYING.  */
varying:
  set_value_range_to_varying (lhs_vr);
  return SSA_PROP_VARYING;
}

#elif (__GNUC__ == 4 && __GNUC_MINOR__ < 8)

/* Return the maximum value for TYPE.  */

static inline tree
vrp_val_max (const_tree type)
{
  if (!INTEGRAL_TYPE_P (type))
    return NULL_TREE;

  return TYPE_MAX_VALUE (type);
}

/* Return the minimum value for TYPE.  */

static inline tree
vrp_val_min (const_tree type)
{
  if (!INTEGRAL_TYPE_P (type))
    return NULL_TREE;

  return TYPE_MIN_VALUE (type);
}

/* Return whether VAL is equal to the maximum value of its type.  This
   will be true for a positive overflow infinity.  We can't do a
   simple equality comparison with TYPE_MAX_VALUE because C typedefs
   and Ada subtypes can produce types whose TYPE_MAX_VALUE is not ==
   to the integer constant with the same value in the type.  */

static inline bool
vrp_val_is_max (const_tree val)
{
  tree type_max = vrp_val_max (TREE_TYPE (val));
  return (val == type_max
	  || (type_max != NULL_TREE
	      && operand_equal_p (val, type_max, 0)));
}

/* Return whether VAL is equal to the minimum value of its type.  This
   will be true for a negative overflow infinity.  */

static inline bool
vrp_val_is_min (const_tree val)
{
  tree type_min = vrp_val_min (TREE_TYPE (val));
  return (val == type_min
	  || (type_min != NULL_TREE
	      && operand_equal_p (val, type_min, 0)));
}


/* Return whether TYPE should use an overflow infinity distinct from
   TYPE_{MIN,MAX}_VALUE.  We use an overflow infinity value to
   represent a signed overflow during VRP computations.  An infinity
   is distinct from a half-range, which will go from some number to
   TYPE_{MIN,MAX}_VALUE.  */

static inline bool
needs_overflow_infinity (const_tree type)
{
  return INTEGRAL_TYPE_P (type) && !TYPE_OVERFLOW_WRAPS (type);
}

/* Return whether TYPE can support our overflow infinity
   representation: we use the TREE_OVERFLOW flag, which only exists
   for constants.  If TYPE doesn't support this, we don't optimize
   cases which would require signed overflow--we drop them to
   VARYING.  */

static inline bool
supports_overflow_infinity (const_tree type)
{
  tree min = vrp_val_min (type), max = vrp_val_max (type);
#ifdef ENABLE_CHECKING
  gcc_assert (needs_overflow_infinity (type));
#endif
  return (min != NULL_TREE
	  && CONSTANT_CLASS_P (min)
	  && max != NULL_TREE
	  && CONSTANT_CLASS_P (max));
}

/* VAL is the maximum or minimum value of a type.  Return a
   corresponding overflow infinity.  */

static inline tree
make_overflow_infinity (tree val)
{
  gcc_checking_assert (val != NULL_TREE && CONSTANT_CLASS_P (val));
  val = copy_node (val);
  TREE_OVERFLOW (val) = 1;
  return val;
}

/* Return a negative overflow infinity for TYPE.  */

static inline tree
negative_overflow_infinity (tree type)
{
  gcc_checking_assert (supports_overflow_infinity (type));
  return make_overflow_infinity (vrp_val_min (type));
}

/* Return a positive overflow infinity for TYPE.  */

static inline tree
positive_overflow_infinity (tree type)
{
  gcc_checking_assert (supports_overflow_infinity (type));
  return make_overflow_infinity (vrp_val_max (type));
}

/* Return whether VAL is a negative overflow infinity.  */

static inline bool
is_negative_overflow_infinity (const_tree val)
{
  return (needs_overflow_infinity (TREE_TYPE (val))
	  && CONSTANT_CLASS_P (val)
	  && TREE_OVERFLOW (val)
	  && vrp_val_is_min (val));
}

/* Return whether VAL is a positive overflow infinity.  */

static inline bool
is_positive_overflow_infinity (const_tree val)
{
  return (needs_overflow_infinity (TREE_TYPE (val))
	  && CONSTANT_CLASS_P (val)
	  && TREE_OVERFLOW (val)
	  && vrp_val_is_max (val));
}

/* Return whether VAL is a positive or negative overflow infinity.  */

static inline bool
is_overflow_infinity (const_tree val)
{
  return (needs_overflow_infinity (TREE_TYPE (val))
	  && CONSTANT_CLASS_P (val)
	  && TREE_OVERFLOW (val)
	  && (vrp_val_is_min (val) || vrp_val_is_max (val)));
}

/* Return whether STMT has a constant rhs that is_overflow_infinity. */

static inline bool
stmt_overflow_infinity (GIMPLE_type stmt)
{
  if (is_gimple_assign (stmt)
      && get_gimple_rhs_class (gimple_assign_rhs_code (stmt)) ==
      GIMPLE_SINGLE_RHS)
    return is_overflow_infinity (gimple_assign_rhs1 (stmt));
  return false;
}

/* If VAL is now an overflow infinity, return VAL.  Otherwise, return
   the same value with TREE_OVERFLOW clear.  This can be used to avoid
   confusing a regular value with an overflow value.  */

static inline tree
avoid_overflow_infinity (tree val)
{
  if (!is_overflow_infinity (val))
    return val;

  if (vrp_val_is_max (val))
    return vrp_val_max (TREE_TYPE (val));
  else
    {
      gcc_checking_assert (vrp_val_is_min (val));
      return vrp_val_min (TREE_TYPE (val));
    }
}


/* Return true if ARG is marked with the nonnull attribute in the
   current function signature.  */

static bool
nonnull_arg_p (const_tree arg)
{
  tree t, attrs, fntype;
  unsigned HOST_WIDE_INT arg_num;

  gcc_assert (TREE_CODE (arg) == PARM_DECL && POINTER_TYPE_P (TREE_TYPE (arg)));

  /* The static chain decl is always non null.  */
  if (arg == cfun->static_chain_decl)
    return true;

  fntype = TREE_TYPE (current_function_decl);
  attrs = lookup_attribute ("nonnull", TYPE_ATTRIBUTES (fntype));

  /* If "nonnull" wasn't specified, we know nothing about the argument.  */
  if (attrs == NULL_TREE)
    return false;

  /* If "nonnull" applies to all the arguments, then ARG is non-null.  */
  if (TREE_VALUE (attrs) == NULL_TREE)
    return true;

  /* Get the position number for ARG in the function signature.  */
  for (arg_num = 1, t = DECL_ARGUMENTS (current_function_decl);
       t;
       t = DECL_CHAIN (t), arg_num++)
    {
      if (t == arg)
	break;
    }

  gcc_assert (t == arg);

  /* Now see if ARG_NUM is mentioned in the nonnull list.  */
  for (t = TREE_VALUE (attrs); t; t = TREE_CHAIN (t))
    {
      if (compare_tree_int (TREE_VALUE (t), arg_num) == 0)
	return true;
    }

  return false;
}


/* Set value range VR to VR_VARYING.  */

static inline void
set_value_range_to_varying (value_range_t *vr)
{
  vr->type = VR_VARYING;
  vr->min = vr->max = NULL_TREE;
  if (vr->equiv)
    bitmap_clear (vr->equiv);
}


/* Set value range VR to {T, MIN, MAX, EQUIV}.  */

static void
set_value_range (value_range_t *vr, enum value_range_type t, tree min,
		 tree max, bitmap equiv)
{
#if defined ENABLE_CHECKING
  /* Check the validity of the range.  */
  if (t == VR_RANGE || t == VR_ANTI_RANGE)
    {
      int cmp;

      gcc_assert (min && max);

      if (INTEGRAL_TYPE_P (TREE_TYPE (min)) && t == VR_ANTI_RANGE)
	gcc_assert (!vrp_val_is_min (min) || !vrp_val_is_max (max));

      cmp = compare_values (min, max);
      gcc_assert (cmp == 0 || cmp == -1 || cmp == -2);

      if (needs_overflow_infinity (TREE_TYPE (min)))
	gcc_assert (!is_overflow_infinity (min)
		    || !is_overflow_infinity (max));
    }

  if (t == VR_UNDEFINED || t == VR_VARYING)
    gcc_assert (min == NULL_TREE && max == NULL_TREE);

  if (t == VR_UNDEFINED || t == VR_VARYING)
    gcc_assert (equiv == NULL || bitmap_empty_p (equiv));
#endif

  vr->type = t;
  vr->min = min;
  vr->max = max;

  /* Since updating the equivalence set involves deep copying the
     bitmaps, only do it if absolutely necessary.  */
  if (vr->equiv == NULL
      && equiv != NULL)
    vr->equiv = BITMAP_ALLOC (NULL);

  if (equiv != vr->equiv)
    {
      if (equiv && !bitmap_empty_p (equiv))
	bitmap_copy (vr->equiv, equiv);
      else
	bitmap_clear (vr->equiv);
    }
}


/* Set value range VR to the canonical form of {T, MIN, MAX, EQUIV}.
   This means adjusting T, MIN and MAX representing the case of a
   wrapping range with MAX < MIN covering [MIN, type_max] U [type_min, MAX]
   as anti-rage ~[MAX+1, MIN-1].  Likewise for wrapping anti-ranges.
   In corner cases where MAX+1 or MIN-1 wraps this will fall back
   to varying.
   This routine exists to ease canonicalization in the case where we
   extract ranges from var + CST op limit.  */

static void
set_and_canonicalize_value_range (value_range_t *vr, enum value_range_type t,
				  tree min, tree max, bitmap equiv)
{
  /* Nothing to canonicalize for symbolic or unknown or varying ranges.  */
  if ((t != VR_RANGE
       && t != VR_ANTI_RANGE)
      || TREE_CODE (min) != INTEGER_CST
      || TREE_CODE (max) != INTEGER_CST)
    {
      set_value_range (vr, t, min, max, equiv);
      return;
    }

  /* Wrong order for min and max, to swap them and the VR type we need
     to adjust them.  */
  if (tree_int_cst_lt (max, min))
    {
      tree one = build_int_cst (TREE_TYPE (min), 1);
      tree tmp = int_const_binop (PLUS_EXPR, max, one);
      max = int_const_binop (MINUS_EXPR, min, one);
      min = tmp;

      /* There's one corner case, if we had [C+1, C] before we now have
	 that again.  But this represents an empty value range, so drop
	 to varying in this case.  */
      if (tree_int_cst_lt (max, min))
	{
	  set_value_range_to_varying (vr);
	  return;
	}

      t = t == VR_RANGE ? VR_ANTI_RANGE : VR_RANGE;
    }

  /* Anti-ranges that can be represented as ranges should be so.  */
  if (t == VR_ANTI_RANGE)
    {
      bool is_min = vrp_val_is_min (min);
      bool is_max = vrp_val_is_max (max);

      if (is_min && is_max)
	{
	  /* We cannot deal with empty ranges, drop to varying.  */
	  set_value_range_to_varying (vr);
	  return;
	}
      else if (is_min
	       /* As a special exception preserve non-null ranges.  */
	       && !(TYPE_UNSIGNED (TREE_TYPE (min))
		    && integer_zerop (max)))
        {
	  tree one = build_int_cst (TREE_TYPE (max), 1);
	  min = int_const_binop (PLUS_EXPR, max, one);
	  max = vrp_val_max (TREE_TYPE (max));
	  t = VR_RANGE;
        }
      else if (is_max)
        {
	  tree one = build_int_cst (TREE_TYPE (min), 1);
	  max = int_const_binop (MINUS_EXPR, min, one);
	  min = vrp_val_min (TREE_TYPE (min));
	  t = VR_RANGE;
        }
    }

  set_value_range (vr, t, min, max, equiv);
}

/* Copy value range FROM into value range TO.  */

static inline void
copy_value_range (value_range_t *to, value_range_t *from)
{
  set_value_range (to, from->type, from->min, from->max, from->equiv);
}

/* Set value range VR to a single value.  This function is only called
   with values we get from statements, and exists to clear the
   TREE_OVERFLOW flag so that we don't think we have an overflow
   infinity when we shouldn't.  */

static inline void
set_value_range_to_value (value_range_t *vr, tree val, bitmap equiv)
{
  gcc_assert (is_gimple_min_invariant (val));
  val = avoid_overflow_infinity (val);
  set_value_range (vr, VR_RANGE, val, val, equiv);
}

/* Set value range VR to a non-negative range of type TYPE.
   OVERFLOW_INFINITY indicates whether to use an overflow infinity
   rather than TYPE_MAX_VALUE; this should be true if we determine
   that the range is nonnegative based on the assumption that signed
   overflow does not occur.  */

static inline void
set_value_range_to_nonnegative (value_range_t *vr, tree type,
				bool overflow_infinity)
{
  tree zero;

  if (overflow_infinity && !supports_overflow_infinity (type))
    {
      set_value_range_to_varying (vr);
      return;
    }

  zero = build_int_cst (type, 0);
  set_value_range (vr, VR_RANGE, zero,
		   (overflow_infinity
		    ? positive_overflow_infinity (type)
		    : TYPE_MAX_VALUE (type)),
		   vr->equiv);
}

/* Set value range VR to a non-NULL range of type TYPE.  */

static inline void
set_value_range_to_nonnull (value_range_t *vr, tree type)
{
  tree zero = build_int_cst (type, 0);
  set_value_range (vr, VR_ANTI_RANGE, zero, zero, vr->equiv);
}


/* Set value range VR to a NULL range of type TYPE.  */

static inline void
set_value_range_to_null (value_range_t *vr, tree type)
{
  set_value_range_to_value (vr, build_int_cst (type, 0), vr->equiv);
}


/* Set value range VR to a range of a truthvalue of type TYPE.  */

static inline void
set_value_range_to_truthvalue (value_range_t *vr, tree type)
{
  if (TYPE_PRECISION (type) == 1)
    set_value_range_to_varying (vr);
  else
    set_value_range (vr, VR_RANGE,
		     build_int_cst (type, 0), build_int_cst (type, 1),
		     vr->equiv);
}


/* Set value range VR to VR_UNDEFINED.  */

static inline void
set_value_range_to_undefined (value_range_t *vr)
{
  vr->type = VR_UNDEFINED;
  vr->min = vr->max = NULL_TREE;
  if (vr->equiv)
    bitmap_clear (vr->equiv);
}


/* If abs (min) < abs (max), set VR to [-max, max], if
   abs (min) >= abs (max), set VR to [-min, min].  */

static void
abs_extent_range (value_range_t *vr, tree min, tree max)
{
  int cmp;

  gcc_assert (TREE_CODE (min) == INTEGER_CST);
  gcc_assert (TREE_CODE (max) == INTEGER_CST);
  gcc_assert (INTEGRAL_TYPE_P (TREE_TYPE (min)));
  gcc_assert (!TYPE_UNSIGNED (TREE_TYPE (min)));
  min = fold_unary (ABS_EXPR, TREE_TYPE (min), min);
  max = fold_unary (ABS_EXPR, TREE_TYPE (max), max);
  if (TREE_OVERFLOW (min) || TREE_OVERFLOW (max))
    {
      set_value_range_to_varying (vr);
      return;
    }
  cmp = compare_values (min, max);
  if (cmp == -1)
    min = fold_unary (NEGATE_EXPR, TREE_TYPE (min), max);
  else if (cmp == 0 || cmp == 1)
    {
      max = min;
      min = fold_unary (NEGATE_EXPR, TREE_TYPE (min), min);
    }
  else
    {
      set_value_range_to_varying (vr);
      return;
    }
  set_and_canonicalize_value_range (vr, VR_RANGE, min, max, NULL);
}


/* Return value range information for VAR.

   If we have no values ranges recorded (ie, VRP is not running), then
   return NULL.  Otherwise create an empty range if none existed for VAR.  */

static value_range_t *
get_value_range (const_tree var)
{
  static const struct value_range_d vr_const_varying
    = { VR_VARYING, NULL_TREE, NULL_TREE, NULL };
  value_range_t *vr;
  tree sym;
  unsigned ver = SSA_NAME_VERSION (var);

  /* If we have no recorded ranges, then return NULL.  */
  if (! vr_value)
    return NULL;

  /* If we query the range for a new SSA name return an unmodifiable VARYING.
     We should get here at most from the substitute-and-fold stage which
     will never try to change values.  */
  if (ver >= num_vr_values)
    return CONST_CAST (value_range_t *, &vr_const_varying);

  vr = vr_value[ver];
  if (vr)
    return vr;

  /* After propagation finished do not allocate new value-ranges.  */
  if (values_propagated)
    return CONST_CAST (value_range_t *, &vr_const_varying);

  /* Create a default value range.  */
  vr_value[ver] = vr = XCNEW (value_range_t);

  /* Defer allocating the equivalence set.  */
  vr->equiv = NULL;

  /* If VAR is a default definition of a parameter, the variable can
     take any value in VAR's type.  */
  sym = SSA_NAME_VAR (var);
  if (SSA_NAME_IS_DEFAULT_DEF (var))
    {
      if (TREE_CODE (sym) == PARM_DECL)
	{
	  /* Try to use the "nonnull" attribute to create ~[0, 0]
	     anti-ranges for pointers.  Note that this is only valid with
	     default definitions of PARM_DECLs.  */
	  if (POINTER_TYPE_P (TREE_TYPE (sym))
	      && nonnull_arg_p (sym))
	    set_value_range_to_nonnull (vr, TREE_TYPE (sym));
	  else
	    set_value_range_to_varying (vr);
	}
      else if (TREE_CODE (sym) == RESULT_DECL
	       && DECL_BY_REFERENCE (sym))
	set_value_range_to_nonnull (vr, TREE_TYPE (sym));
    }

  return vr;
}

/* Return true, if VAL1 and VAL2 are equal values for VRP purposes.  */

static inline bool
vrp_operand_equal_p (const_tree val1, const_tree val2)
{
  if (val1 == val2)
    return true;
  if (!val1 || !val2 || !operand_equal_p (val1, val2, 0))
    return false;
  if (is_overflow_infinity (val1))
    return is_overflow_infinity (val2);
  return true;
}

/* Return true, if the bitmaps B1 and B2 are equal.  */

static inline bool
vrp_bitmap_equal_p (const_bitmap b1, const_bitmap b2)
{
  return (b1 == b2
	  || ((!b1 || bitmap_empty_p (b1))
	      && (!b2 || bitmap_empty_p (b2)))
	  || (b1 && b2
	      && bitmap_equal_p (b1, b2)));
}

/* Update the value range and equivalence set for variable VAR to
   NEW_VR.  Return true if NEW_VR is different from VAR's previous
   value.

   NOTE: This function assumes that NEW_VR is a temporary value range
   object created for the sole purpose of updating VAR's range.  The
   storage used by the equivalence set from NEW_VR will be freed by
   this function.  Do not call update_value_range when NEW_VR
   is the range object associated with another SSA name.  */

static inline bool
update_value_range (const_tree var, value_range_t *new_vr)
{
  value_range_t *old_vr;
  bool is_new;

  /* Update the value range, if necessary.  */
  old_vr = get_value_range (var);
  is_new = old_vr->type != new_vr->type
	   || !vrp_operand_equal_p (old_vr->min, new_vr->min)
	   || !vrp_operand_equal_p (old_vr->max, new_vr->max)
	   || !vrp_bitmap_equal_p (old_vr->equiv, new_vr->equiv);

  if (is_new)
    set_value_range (old_vr, new_vr->type, new_vr->min, new_vr->max,
	             new_vr->equiv);

  BITMAP_FREE (new_vr->equiv);

  return is_new;
}


/* Add VAR and VAR's equivalence set to EQUIV.  This is the central
   point where equivalence processing can be turned on/off.  */

static void
add_equivalence (bitmap *equiv, const_tree var)
{
  unsigned ver = SSA_NAME_VERSION (var);
  value_range_t *vr = vr_value[ver];

  if (*equiv == NULL)
    *equiv = BITMAP_ALLOC (NULL);
  bitmap_set_bit (*equiv, ver);
  if (vr && vr->equiv)
    bitmap_ior_into (*equiv, vr->equiv);
}


/* Return true if VR is ~[0, 0].  */

static inline bool
range_is_nonnull (value_range_t *vr)
{
  return vr->type == VR_ANTI_RANGE
	 && integer_zerop (vr->min)
	 && integer_zerop (vr->max);
}


/* Return true if VR is [0, 0].  */

static inline bool
range_is_null (value_range_t *vr)
{
  return vr->type == VR_RANGE
	 && integer_zerop (vr->min)
	 && integer_zerop (vr->max);
}

/* Return true if max and min of VR are INTEGER_CST.  It's not necessary
   a singleton.  */

static inline bool
range_int_cst_p (value_range_t *vr)
{
  return (vr->type == VR_RANGE
	  && TREE_CODE (vr->max) == INTEGER_CST
	  && TREE_CODE (vr->min) == INTEGER_CST
	  && !TREE_OVERFLOW (vr->max)
	  && !TREE_OVERFLOW (vr->min));
}

/* Return true if VR is a INTEGER_CST singleton.  */

static inline bool
range_int_cst_singleton_p (value_range_t *vr)
{
  return (range_int_cst_p (vr)
	  && tree_int_cst_equal (vr->min, vr->max));
}

/* Return true if value range VR involves at least one symbol.  */

static inline bool
symbolic_range_p (value_range_t *vr)
{
  return (!is_gimple_min_invariant (vr->min)
          || !is_gimple_min_invariant (vr->max));
}

/* Return true if value range VR uses an overflow infinity.  */

static inline bool
overflow_infinity_range_p (value_range_t *vr)
{
  return (vr->type == VR_RANGE
	  && (is_overflow_infinity (vr->min)
	      || is_overflow_infinity (vr->max)));
}

/* Return false if we can not make a valid comparison based on VR;
   this will be the case if it uses an overflow infinity and overflow
   is not undefined (i.e., -fno-strict-overflow is in effect).
   Otherwise return true, and set *STRICT_OVERFLOW_P to true if VR
   uses an overflow infinity.  */

static bool
usable_range_p (value_range_t *vr, bool *strict_overflow_p)
{
  gcc_assert (vr->type == VR_RANGE);
  if (is_overflow_infinity (vr->min))
    {
      *strict_overflow_p = true;
      if (!TYPE_OVERFLOW_UNDEFINED (TREE_TYPE (vr->min)))
	return false;
    }
  if (is_overflow_infinity (vr->max))
    {
      *strict_overflow_p = true;
      if (!TYPE_OVERFLOW_UNDEFINED (TREE_TYPE (vr->max)))
	return false;
    }
  return true;
}


/* Return true if the result of assignment STMT is know to be non-negative.
   If the return value is based on the assumption that signed overflow is
   undefined, set *STRICT_OVERFLOW_P to true; otherwise, don't change
   *STRICT_OVERFLOW_P.*/

static bool
gimple_assign_nonnegative_warnv_p (GIMPLE_type stmt, bool *strict_overflow_p)
{
  enum tree_code code = gimple_assign_rhs_code (stmt);
  switch (get_gimple_rhs_class (code))
    {
    case GIMPLE_UNARY_RHS:
      return tree_unary_nonnegative_warnv_p (gimple_assign_rhs_code (stmt),
					     gimple_expr_type (stmt),
					     gimple_assign_rhs1 (stmt),
					     strict_overflow_p);
    case GIMPLE_BINARY_RHS:
      return tree_binary_nonnegative_warnv_p (gimple_assign_rhs_code (stmt),
					      gimple_expr_type (stmt),
					      gimple_assign_rhs1 (stmt),
					      gimple_assign_rhs2 (stmt),
					      strict_overflow_p);
    case GIMPLE_TERNARY_RHS:
      return false;
    case GIMPLE_SINGLE_RHS:
      return tree_single_nonnegative_warnv_p (gimple_assign_rhs1 (stmt),
					      strict_overflow_p);
    case GIMPLE_INVALID_RHS:
      gcc_unreachable ();
    default:
      gcc_unreachable ();
    }
}

/* Return true if return value of call STMT is know to be non-negative.
   If the return value is based on the assumption that signed overflow is
   undefined, set *STRICT_OVERFLOW_P to true; otherwise, don't change
   *STRICT_OVERFLOW_P.*/

static bool
gimple_call_nonnegative_warnv_p (GIMPLE_type stmt, bool *strict_overflow_p)
{
  tree arg0 = gimple_call_num_args (stmt) > 0 ?
    gimple_call_arg (stmt, 0) : NULL_TREE;
  tree arg1 = gimple_call_num_args (stmt) > 1 ?
    gimple_call_arg (stmt, 1) : NULL_TREE;

  return tree_call_nonnegative_warnv_p (gimple_expr_type (stmt),
					gimple_call_fndecl (stmt),
					arg0,
					arg1,
					strict_overflow_p);
}

/* Return true if STMT is know to to compute a non-negative value.
   If the return value is based on the assumption that signed overflow is
   undefined, set *STRICT_OVERFLOW_P to true; otherwise, don't change
   *STRICT_OVERFLOW_P.*/

static bool
gimple_stmt_nonnegative_warnv_p (GIMPLE_type stmt, bool *strict_overflow_p)
{
  switch (gimple_code (stmt))
    {
    case GIMPLE_ASSIGN:
      return gimple_assign_nonnegative_warnv_p (stmt, strict_overflow_p);
    case GIMPLE_CALL:
      return gimple_call_nonnegative_warnv_p (stmt, strict_overflow_p);
    default:
      gcc_unreachable ();
    }
}

/* Return true if the result of assignment STMT is know to be non-zero.
   If the return value is based on the assumption that signed overflow is
   undefined, set *STRICT_OVERFLOW_P to true; otherwise, don't change
   *STRICT_OVERFLOW_P.*/

static bool
gimple_assign_nonzero_warnv_p (GIMPLE_type stmt, bool *strict_overflow_p)
{
  enum tree_code code = gimple_assign_rhs_code (stmt);
  switch (get_gimple_rhs_class (code))
    {
    case GIMPLE_UNARY_RHS:
      return tree_unary_nonzero_warnv_p (gimple_assign_rhs_code (stmt),
					 gimple_expr_type (stmt),
					 gimple_assign_rhs1 (stmt),
					 strict_overflow_p);
    case GIMPLE_BINARY_RHS:
      return tree_binary_nonzero_warnv_p (gimple_assign_rhs_code (stmt),
					  gimple_expr_type (stmt),
					  gimple_assign_rhs1 (stmt),
					  gimple_assign_rhs2 (stmt),
					  strict_overflow_p);
    case GIMPLE_TERNARY_RHS:
      return false;
    case GIMPLE_SINGLE_RHS:
      return tree_single_nonzero_warnv_p (gimple_assign_rhs1 (stmt),
					  strict_overflow_p);
    case GIMPLE_INVALID_RHS:
      gcc_unreachable ();
    default:
      gcc_unreachable ();
    }
}

/* Return true if STMT is know to to compute a non-zero value.
   If the return value is based on the assumption that signed overflow is
   undefined, set *STRICT_OVERFLOW_P to true; otherwise, don't change
   *STRICT_OVERFLOW_P.*/

static bool
gimple_stmt_nonzero_warnv_p (GIMPLE_type stmt, bool *strict_overflow_p)
{
  switch (gimple_code (stmt))
    {
    case GIMPLE_ASSIGN:
      return gimple_assign_nonzero_warnv_p (stmt, strict_overflow_p);
    case GIMPLE_CALL:
      return gimple_alloca_call_p (stmt);
    default:
      gcc_unreachable ();
    }
}

/* Like tree_expr_nonzero_warnv_p, but this function uses value ranges
   obtained so far.  */

static bool
vrp_stmt_computes_nonzero (GIMPLE_type stmt, bool *strict_overflow_p)
{
  if (gimple_stmt_nonzero_warnv_p (stmt, strict_overflow_p))
    return true;

  /* If we have an expression of the form &X->a, then the expression
     is nonnull if X is nonnull.  */
  if (is_gimple_assign (stmt)
      && gimple_assign_rhs_code (stmt) == ADDR_EXPR)
    {
      tree expr = gimple_assign_rhs1 (stmt);
      tree base = get_base_address (TREE_OPERAND (expr, 0));

      if (base != NULL_TREE
	  && TREE_CODE (base) == MEM_REF
	  && TREE_CODE (TREE_OPERAND (base, 0)) == SSA_NAME)
	{
	  value_range_t *vr = get_value_range (TREE_OPERAND (base, 0));
	  if (range_is_nonnull (vr))
	    return true;
	}
    }

  return false;
}

/* Returns true if EXPR is a valid value (as expected by compare_values) --
   a GIMPLE_type invariant, or SSA_NAME +- CST.  */

static bool
valid_value_p (tree expr)
{
  if (TREE_CODE (expr) == SSA_NAME)
    return true;

  if (TREE_CODE (expr) == PLUS_EXPR
      || TREE_CODE (expr) == MINUS_EXPR)
    return (TREE_CODE (TREE_OPERAND (expr, 0)) == SSA_NAME
	    && TREE_CODE (TREE_OPERAND (expr, 1)) == INTEGER_CST);

  return is_gimple_min_invariant (expr);
}

/* Return
   1 if VAL < VAL2
   0 if !(VAL < VAL2)
   -2 if those are incomparable.  */
static inline int
operand_less_p (tree val, tree val2)
{
  /* LT is folded faster than GE and others.  Inline the common case.  */
  if (TREE_CODE (val) == INTEGER_CST && TREE_CODE (val2) == INTEGER_CST)
    {
      if (TYPE_UNSIGNED (TREE_TYPE (val)))
	return INT_CST_LT_UNSIGNED (val, val2);
      else
	{
	  if (INT_CST_LT (val, val2))
	    return 1;
	}
    }
  else
    {
      tree tcmp;

      fold_defer_overflow_warnings ();

      tcmp = fold_binary_to_constant (LT_EXPR, boolean_type_node, val, val2);

      fold_undefer_and_ignore_overflow_warnings ();

      if (!tcmp
	  || TREE_CODE (tcmp) != INTEGER_CST)
	return -2;

      if (!integer_zerop (tcmp))
	return 1;
    }

  /* val >= val2, not considering overflow infinity.  */
  if (is_negative_overflow_infinity (val))
    return is_negative_overflow_infinity (val2) ? 0 : 1;
  else if (is_positive_overflow_infinity (val2))
    return is_positive_overflow_infinity (val) ? 0 : 1;

  return 0;
}

/* Compare two values VAL1 and VAL2.  Return

   	-2 if VAL1 and VAL2 cannot be compared at compile-time,
   	-1 if VAL1 < VAL2,
   	 0 if VAL1 == VAL2,
	+1 if VAL1 > VAL2, and
	+2 if VAL1 != VAL2

   This is similar to tree_int_cst_compare but supports pointer values
   and values that cannot be compared at compile time.

   If STRICT_OVERFLOW_P is not NULL, then set *STRICT_OVERFLOW_P to
   true if the return value is only valid if we assume that signed
   overflow is undefined.  */

static int
compare_values_warnv (tree val1, tree val2, bool *strict_overflow_p)
{
  if (val1 == val2)
    return 0;

  /* Below we rely on the fact that VAL1 and VAL2 are both pointers or
     both integers.  */
  gcc_assert (POINTER_TYPE_P (TREE_TYPE (val1))
	      == POINTER_TYPE_P (TREE_TYPE (val2)));
  /* Convert the two values into the same type.  This is needed because
     sizetype causes sign extension even for unsigned types.  */
  val2 = fold_convert (TREE_TYPE (val1), val2);
  STRIP_USELESS_TYPE_CONVERSION (val2);

  if ((TREE_CODE (val1) == SSA_NAME
       || TREE_CODE (val1) == PLUS_EXPR
       || TREE_CODE (val1) == MINUS_EXPR)
      && (TREE_CODE (val2) == SSA_NAME
	  || TREE_CODE (val2) == PLUS_EXPR
	  || TREE_CODE (val2) == MINUS_EXPR))
    {
      tree n1, c1, n2, c2;
      enum tree_code code1, code2;

      /* If VAL1 and VAL2 are of the form 'NAME [+-] CST' or 'NAME',
	 return -1 or +1 accordingly.  If VAL1 and VAL2 don't use the
	 same name, return -2.  */
      if (TREE_CODE (val1) == SSA_NAME)
	{
	  code1 = SSA_NAME;
	  n1 = val1;
	  c1 = NULL_TREE;
	}
      else
	{
	  code1 = TREE_CODE (val1);
	  n1 = TREE_OPERAND (val1, 0);
	  c1 = TREE_OPERAND (val1, 1);
	  if (tree_int_cst_sgn (c1) == -1)
	    {
	      if (is_negative_overflow_infinity (c1))
		return -2;
	      c1 = fold_unary_to_constant (NEGATE_EXPR, TREE_TYPE (c1), c1);
	      if (!c1)
		return -2;
	      code1 = code1 == MINUS_EXPR ? PLUS_EXPR : MINUS_EXPR;
	    }
	}

      if (TREE_CODE (val2) == SSA_NAME)
	{
	  code2 = SSA_NAME;
	  n2 = val2;
	  c2 = NULL_TREE;
	}
      else
	{
	  code2 = TREE_CODE (val2);
	  n2 = TREE_OPERAND (val2, 0);
	  c2 = TREE_OPERAND (val2, 1);
	  if (tree_int_cst_sgn (c2) == -1)
	    {
	      if (is_negative_overflow_infinity (c2))
		return -2;
	      c2 = fold_unary_to_constant (NEGATE_EXPR, TREE_TYPE (c2), c2);
	      if (!c2)
		return -2;
	      code2 = code2 == MINUS_EXPR ? PLUS_EXPR : MINUS_EXPR;
	    }
	}

      /* Both values must use the same name.  */
      if (n1 != n2)
	return -2;

      if (code1 == SSA_NAME
	  && code2 == SSA_NAME)
	/* NAME == NAME  */
	return 0;

      /* If overflow is defined we cannot simplify more.  */
      if (!TYPE_OVERFLOW_UNDEFINED (TREE_TYPE (val1)))
	return -2;

      if (strict_overflow_p != NULL
	  && (code1 == SSA_NAME || !TREE_NO_WARNING (val1))
	  && (code2 == SSA_NAME || !TREE_NO_WARNING (val2)))
	*strict_overflow_p = true;

      if (code1 == SSA_NAME)
	{
	  if (code2 == PLUS_EXPR)
	    /* NAME < NAME + CST  */
	    return -1;
	  else if (code2 == MINUS_EXPR)
	    /* NAME > NAME - CST  */
	    return 1;
	}
      else if (code1 == PLUS_EXPR)
	{
	  if (code2 == SSA_NAME)
	    /* NAME + CST > NAME  */
	    return 1;
	  else if (code2 == PLUS_EXPR)
	    /* NAME + CST1 > NAME + CST2, if CST1 > CST2  */
	    return compare_values_warnv (c1, c2, strict_overflow_p);
	  else if (code2 == MINUS_EXPR)
	    /* NAME + CST1 > NAME - CST2  */
	    return 1;
	}
      else if (code1 == MINUS_EXPR)
	{
	  if (code2 == SSA_NAME)
	    /* NAME - CST < NAME  */
	    return -1;
	  else if (code2 == PLUS_EXPR)
	    /* NAME - CST1 < NAME + CST2  */
	    return -1;
	  else if (code2 == MINUS_EXPR)
	    /* NAME - CST1 > NAME - CST2, if CST1 < CST2.  Notice that
	       C1 and C2 are swapped in the call to compare_values.  */
	    return compare_values_warnv (c2, c1, strict_overflow_p);
	}

      gcc_unreachable ();
    }

  /* We cannot compare non-constants.  */
  if (!is_gimple_min_invariant (val1) || !is_gimple_min_invariant (val2))
    return -2;

  if (!POINTER_TYPE_P (TREE_TYPE (val1)))
    {
      /* We cannot compare overflowed values, except for overflow
	 infinities.  */
      if (TREE_OVERFLOW (val1) || TREE_OVERFLOW (val2))
	{
	  if (strict_overflow_p != NULL)
	    *strict_overflow_p = true;
	  if (is_negative_overflow_infinity (val1))
	    return is_negative_overflow_infinity (val2) ? 0 : -1;
	  else if (is_negative_overflow_infinity (val2))
	    return 1;
	  else if (is_positive_overflow_infinity (val1))
	    return is_positive_overflow_infinity (val2) ? 0 : 1;
	  else if (is_positive_overflow_infinity (val2))
	    return -1;
	  return -2;
	}

      return tree_int_cst_compare (val1, val2);
    }
  else
    {
      tree t;

      /* First see if VAL1 and VAL2 are not the same.  */
      if (val1 == val2 || operand_equal_p (val1, val2, 0))
	return 0;

      /* If VAL1 is a lower address than VAL2, return -1.  */
      if (operand_less_p (val1, val2) == 1)
	return -1;

      /* If VAL1 is a higher address than VAL2, return +1.  */
      if (operand_less_p (val2, val1) == 1)
	return 1;

      /* If VAL1 is different than VAL2, return +2.
	 For integer constants we either have already returned -1 or 1
	 or they are equivalent.  We still might succeed in proving
	 something about non-trivial operands.  */
      if (TREE_CODE (val1) != INTEGER_CST
	  || TREE_CODE (val2) != INTEGER_CST)
	{
          t = fold_binary_to_constant (NE_EXPR, boolean_type_node, val1, val2);
	  if (t && integer_onep (t))
	    return 2;
	}

      return -2;
    }
}

/* Compare values like compare_values_warnv, but treat comparisons of
   nonconstants which rely on undefined overflow as incomparable.  */

static int
compare_values (tree val1, tree val2)
{
  bool sop;
  int ret;

  sop = false;
  ret = compare_values_warnv (val1, val2, &sop);
  if (sop
      && (!is_gimple_min_invariant (val1) || !is_gimple_min_invariant (val2)))
    ret = -2;
  return ret;
}


/* Return 1 if VAL is inside value range VR (VR->MIN <= VAL <= VR->MAX),
          0 if VAL is not inside VR,
	 -2 if we cannot tell either way.

   FIXME, the current semantics of this functions are a bit quirky
	  when taken in the context of VRP.  In here we do not care
	  about VR's type.  If VR is the anti-range ~[3, 5] the call
	  value_inside_range (4, VR) will return 1.

	  This is counter-intuitive in a strict sense, but the callers
	  currently expect this.  They are calling the function
	  merely to determine whether VR->MIN <= VAL <= VR->MAX.  The
	  callers are applying the VR_RANGE/VR_ANTI_RANGE semantics
	  themselves.

	  This also applies to value_ranges_intersect_p and
	  range_includes_zero_p.  The semantics of VR_RANGE and
	  VR_ANTI_RANGE should be encoded here, but that also means
	  adapting the users of these functions to the new semantics.

   Benchmark compile/20001226-1.c compilation time after changing this
   function.  */

static inline int
value_inside_range (tree val, value_range_t * vr)
{
  int cmp1, cmp2;

  cmp1 = operand_less_p (val, vr->min);
  if (cmp1 == -2)
    return -2;
  if (cmp1 == 1)
    return 0;

  cmp2 = operand_less_p (vr->max, val);
  if (cmp2 == -2)
    return -2;

  return !cmp2;
}


/* Return true if value ranges VR0 and VR1 have a non-empty
   intersection.

   Benchmark compile/20001226-1.c compilation time after changing this
   function.
   */

static inline bool
value_ranges_intersect_p (value_range_t *vr0, value_range_t *vr1)
{
  /* The value ranges do not intersect if the maximum of the first range is
     less than the minimum of the second range or vice versa.
     When those relations are unknown, we can't do any better.  */
  if (operand_less_p (vr0->max, vr1->min) != 0)
    return false;
  if (operand_less_p (vr1->max, vr0->min) != 0)
    return false;
  return true;
}


/* Return true if VR includes the value zero, false otherwise.  FIXME,
   currently this will return false for an anti-range like ~[-4, 3].
   This will be wrong when the semantics of value_inside_range are
   modified (currently the users of this function expect these
   semantics).  */

static inline bool
range_includes_zero_p (value_range_t *vr)
{
  tree zero;

  gcc_assert (vr->type != VR_UNDEFINED
              && vr->type != VR_VARYING
	      && !symbolic_range_p (vr));

  zero = build_int_cst (TREE_TYPE (vr->min), 0);
  return (value_inside_range (zero, vr) == 1);
}

/* Return true if *VR is know to only contain nonnegative values.  */

static inline bool
value_range_nonnegative_p (value_range_t *vr)
{
  /* Testing for VR_ANTI_RANGE is not useful here as any anti-range
     which would return a useful value should be encoded as a 
     VR_RANGE.  */
  if (vr->type == VR_RANGE)
    {
      int result = compare_values (vr->min, integer_zero_node);
      return (result == 0 || result == 1);
    }

  return false;
}

/* Return true if T, an SSA_NAME, is known to be nonnegative.  Return
   false otherwise or if no value range information is available.  */

bool
ssa_name_nonnegative_p (const_tree t)
{
  value_range_t *vr = get_value_range (t);

  if (INTEGRAL_TYPE_P (t)
      && TYPE_UNSIGNED (t))
    return true;

  if (!vr)
    return false;

  return value_range_nonnegative_p (vr);
}

/* If *VR has a value rante that is a single constant value return that,
   otherwise return NULL_TREE.  */

static tree
value_range_constant_singleton (value_range_t *vr)
{
  if (vr->type == VR_RANGE
      && operand_equal_p (vr->min, vr->max, 0)
      && is_gimple_min_invariant (vr->min))
    return vr->min;

  return NULL_TREE;
}

/* If OP has a value range with a single constant value return that,
   otherwise return NULL_TREE.  This returns OP itself if OP is a
   constant.  */

static tree
op_with_constant_singleton_value_range (tree op)
{
  if (is_gimple_min_invariant (op))
    return op;

  if (TREE_CODE (op) != SSA_NAME)
    return NULL_TREE;

  return value_range_constant_singleton (get_value_range (op));
}

/* Return true if op is in a boolean [0, 1] value-range.  */

static bool
op_with_boolean_value_range_p (tree op)
{
  value_range_t *vr;

  if (TYPE_PRECISION (TREE_TYPE (op)) == 1)
    return true;

  if (integer_zerop (op)
      || integer_onep (op))
    return true;

  if (TREE_CODE (op) != SSA_NAME)
    return false;

  vr = get_value_range (op);
  return (vr->type == VR_RANGE
	  && integer_zerop (vr->min)
	  && integer_onep (vr->max));
}

/* Extract value range information from an ASSERT_EXPR EXPR and store
   it in *VR_P.  */

static void
extract_range_from_assert (value_range_t *vr_p, tree expr)
{
  tree var, cond, limit, min, max, type;
  value_range_t *var_vr, *limit_vr;
  enum tree_code cond_code;

  var = ASSERT_EXPR_VAR (expr);
  cond = ASSERT_EXPR_COND (expr);

  gcc_assert (COMPARISON_CLASS_P (cond));

  /* Find VAR in the ASSERT_EXPR conditional.  */
  if (var == TREE_OPERAND (cond, 0)
      || TREE_CODE (TREE_OPERAND (cond, 0)) == PLUS_EXPR
      || TREE_CODE (TREE_OPERAND (cond, 0)) == NOP_EXPR)
    {
      /* If the predicate is of the form VAR COMP LIMIT, then we just
	 take LIMIT from the RHS and use the same comparison code.  */
      cond_code = TREE_CODE (cond);
      limit = TREE_OPERAND (cond, 1);
      cond = TREE_OPERAND (cond, 0);
    }
  else
    {
      /* If the predicate is of the form LIMIT COMP VAR, then we need
	 to flip around the comparison code to create the proper range
	 for VAR.  */
      cond_code = swap_tree_comparison (TREE_CODE (cond));
      limit = TREE_OPERAND (cond, 0);
      cond = TREE_OPERAND (cond, 1);
    }

  limit = avoid_overflow_infinity (limit);

  type = TREE_TYPE (var);
  gcc_assert (limit != var);

  /* For pointer arithmetic, we only keep track of pointer equality
     and inequality.  */
  if (POINTER_TYPE_P (type) && cond_code != NE_EXPR && cond_code != EQ_EXPR)
    {
      set_value_range_to_varying (vr_p);
      return;
    }

  /* If LIMIT is another SSA name and LIMIT has a range of its own,
     try to use LIMIT's range to avoid creating symbolic ranges
     unnecessarily. */
  limit_vr = (TREE_CODE (limit) == SSA_NAME) ? get_value_range (limit) : NULL;

  /* LIMIT's range is only interesting if it has any useful information.  */
  if (limit_vr
      && (limit_vr->type == VR_UNDEFINED
	  || limit_vr->type == VR_VARYING
	  || symbolic_range_p (limit_vr)))
    limit_vr = NULL;

  /* Initially, the new range has the same set of equivalences of
     VAR's range.  This will be revised before returning the final
     value.  Since assertions may be chained via mutually exclusive
     predicates, we will need to trim the set of equivalences before
     we are done.  */
  gcc_assert (vr_p->equiv == NULL);
  add_equivalence (&vr_p->equiv, var);

  /* Extract a new range based on the asserted comparison for VAR and
     LIMIT's value range.  Notice that if LIMIT has an anti-range, we
     will only use it for equality comparisons (EQ_EXPR).  For any
     other kind of assertion, we cannot derive a range from LIMIT's
     anti-range that can be used to describe the new range.  For
     instance, ASSERT_EXPR <x_2, x_2 <= b_4>.  If b_4 is ~[2, 10],
     then b_4 takes on the ranges [-INF, 1] and [11, +INF].  There is
     no single range for x_2 that could describe LE_EXPR, so we might
     as well build the range [b_4, +INF] for it.
     One special case we handle is extracting a range from a
     range test encoded as (unsigned)var + CST <= limit.  */
  if (TREE_CODE (cond) == NOP_EXPR
      || TREE_CODE (cond) == PLUS_EXPR)
    {
      if (TREE_CODE (cond) == PLUS_EXPR)
        {
          min = fold_build1 (NEGATE_EXPR, TREE_TYPE (TREE_OPERAND (cond, 1)),
			     TREE_OPERAND (cond, 1));
          max = int_const_binop (PLUS_EXPR, limit, min);
	  cond = TREE_OPERAND (cond, 0);
	}
      else
	{
	  min = build_int_cst (TREE_TYPE (var), 0);
	  max = limit;
	}

      /* Make sure to not set TREE_OVERFLOW on the final type
	 conversion.  We are willingly interpreting large positive
	 unsigned values as negative singed values here.  */
      min = force_fit_type_double (TREE_TYPE (var), tree_to_double_int (min),
				   0, false);
      max = force_fit_type_double (TREE_TYPE (var), tree_to_double_int (max),
				   0, false);

      /* We can transform a max, min range to an anti-range or
         vice-versa.  Use set_and_canonicalize_value_range which does
	 this for us.  */
      if (cond_code == LE_EXPR)
        set_and_canonicalize_value_range (vr_p, VR_RANGE,
					  min, max, vr_p->equiv);
      else if (cond_code == GT_EXPR)
        set_and_canonicalize_value_range (vr_p, VR_ANTI_RANGE,
					  min, max, vr_p->equiv);
      else
	gcc_unreachable ();
    }
  else if (cond_code == EQ_EXPR)
    {
      enum value_range_type range_type;

      if (limit_vr)
	{
	  range_type = limit_vr->type;
	  min = limit_vr->min;
	  max = limit_vr->max;
	}
      else
	{
	  range_type = VR_RANGE;
	  min = limit;
	  max = limit;
	}

      set_value_range (vr_p, range_type, min, max, vr_p->equiv);

      /* When asserting the equality VAR == LIMIT and LIMIT is another
	 SSA name, the new range will also inherit the equivalence set
	 from LIMIT.  */
      if (TREE_CODE (limit) == SSA_NAME)
	add_equivalence (&vr_p->equiv, limit);
    }
  else if (cond_code == NE_EXPR)
    {
      /* As described above, when LIMIT's range is an anti-range and
	 this assertion is an inequality (NE_EXPR), then we cannot
	 derive anything from the anti-range.  For instance, if
	 LIMIT's range was ~[0, 0], the assertion 'VAR != LIMIT' does
	 not imply that VAR's range is [0, 0].  So, in the case of
	 anti-ranges, we just assert the inequality using LIMIT and
	 not its anti-range.

	 If LIMIT_VR is a range, we can only use it to build a new
	 anti-range if LIMIT_VR is a single-valued range.  For
	 instance, if LIMIT_VR is [0, 1], the predicate
	 VAR != [0, 1] does not mean that VAR's range is ~[0, 1].
	 Rather, it means that for value 0 VAR should be ~[0, 0]
	 and for value 1, VAR should be ~[1, 1].  We cannot
	 represent these ranges.

	 The only situation in which we can build a valid
	 anti-range is when LIMIT_VR is a single-valued range
	 (i.e., LIMIT_VR->MIN == LIMIT_VR->MAX).  In that case,
	 build the anti-range ~[LIMIT_VR->MIN, LIMIT_VR->MAX].  */
      if (limit_vr
	  && limit_vr->type == VR_RANGE
	  && compare_values (limit_vr->min, limit_vr->max) == 0)
	{
	  min = limit_vr->min;
	  max = limit_vr->max;
	}
      else
	{
	  /* In any other case, we cannot use LIMIT's range to build a
	     valid anti-range.  */
	  min = max = limit;
	}

      /* If MIN and MAX cover the whole range for their type, then
	 just use the original LIMIT.  */
      if (INTEGRAL_TYPE_P (type)
	  && vrp_val_is_min (min)
	  && vrp_val_is_max (max))
	min = max = limit;

      set_value_range (vr_p, VR_ANTI_RANGE, min, max, vr_p->equiv);
    }
  else if (cond_code == LE_EXPR || cond_code == LT_EXPR)
    {
      min = TYPE_MIN_VALUE (type);

      if (limit_vr == NULL || limit_vr->type == VR_ANTI_RANGE)
	max = limit;
      else
	{
	  /* If LIMIT_VR is of the form [N1, N2], we need to build the
	     range [MIN, N2] for LE_EXPR and [MIN, N2 - 1] for
	     LT_EXPR.  */
	  max = limit_vr->max;
	}

      /* If the maximum value forces us to be out of bounds, simply punt.
	 It would be pointless to try and do anything more since this
	 all should be optimized away above us.  */
      if ((cond_code == LT_EXPR
	   && compare_values (max, min) == 0)
	  || (CONSTANT_CLASS_P (max) && TREE_OVERFLOW (max)))
	set_value_range_to_varying (vr_p);
      else
	{
	  /* For LT_EXPR, we create the range [MIN, MAX - 1].  */
	  if (cond_code == LT_EXPR)
	    {
	      if (TYPE_PRECISION (TREE_TYPE (max)) == 1
		  && !TYPE_UNSIGNED (TREE_TYPE (max)))
		max = fold_build2 (PLUS_EXPR, TREE_TYPE (max), max,
				   build_int_cst (TREE_TYPE (max), -1));
	      else
		max = fold_build2 (MINUS_EXPR, TREE_TYPE (max), max,
				   build_int_cst (TREE_TYPE (max), 1));
	      if (EXPR_P (max))
		TREE_NO_WARNING (max) = 1;
	    }

	  set_value_range (vr_p, VR_RANGE, min, max, vr_p->equiv);
	}
    }
  else if (cond_code == GE_EXPR || cond_code == GT_EXPR)
    {
      max = TYPE_MAX_VALUE (type);

      if (limit_vr == NULL || limit_vr->type == VR_ANTI_RANGE)
	min = limit;
      else
	{
	  /* If LIMIT_VR is of the form [N1, N2], we need to build the
	     range [N1, MAX] for GE_EXPR and [N1 + 1, MAX] for
	     GT_EXPR.  */
	  min = limit_vr->min;
	}

      /* If the minimum value forces us to be out of bounds, simply punt.
	 It would be pointless to try and do anything more since this
	 all should be optimized away above us.  */
      if ((cond_code == GT_EXPR
	   && compare_values (min, max) == 0)
	  || (CONSTANT_CLASS_P (min) && TREE_OVERFLOW (min)))
	set_value_range_to_varying (vr_p);
      else
	{
	  /* For GT_EXPR, we create the range [MIN + 1, MAX].  */
	  if (cond_code == GT_EXPR)
	    {
	      if (TYPE_PRECISION (TREE_TYPE (min)) == 1
		  && !TYPE_UNSIGNED (TREE_TYPE (min)))
		min = fold_build2 (MINUS_EXPR, TREE_TYPE (min), min,
				   build_int_cst (TREE_TYPE (min), -1));
	      else
		min = fold_build2 (PLUS_EXPR, TREE_TYPE (min), min,
				   build_int_cst (TREE_TYPE (min), 1));
	      if (EXPR_P (min))
		TREE_NO_WARNING (min) = 1;
	    }

	  set_value_range (vr_p, VR_RANGE, min, max, vr_p->equiv);
	}
    }
  else
    gcc_unreachable ();

  /* If VAR already had a known range, it may happen that the new
     range we have computed and VAR's range are not compatible.  For
     instance,

	if (p_5 == NULL)
	  p_6 = ASSERT_EXPR <p_5, p_5 == NULL>;
	  x_7 = p_6->fld;
	  p_8 = ASSERT_EXPR <p_6, p_6 != NULL>;

     While the above comes from a faulty program, it will cause an ICE
     later because p_8 and p_6 will have incompatible ranges and at
     the same time will be considered equivalent.  A similar situation
     would arise from

     	if (i_5 > 10)
	  i_6 = ASSERT_EXPR <i_5, i_5 > 10>;
	  if (i_5 < 5)
	    i_7 = ASSERT_EXPR <i_6, i_6 < 5>;

     Again i_6 and i_7 will have incompatible ranges.  It would be
     pointless to try and do anything with i_7's range because
     anything dominated by 'if (i_5 < 5)' will be optimized away.
     Note, due to the wa in which simulation proceeds, the statement
     i_7 = ASSERT_EXPR <...> we would never be visited because the
     conditional 'if (i_5 < 5)' always evaluates to false.  However,
     this extra check does not hurt and may protect against future
     changes to VRP that may get into a situation similar to the
     NULL pointer dereference example.

     Note that these compatibility tests are only needed when dealing
     with ranges or a mix of range and anti-range.  If VAR_VR and VR_P
     are both anti-ranges, they will always be compatible, because two
     anti-ranges will always have a non-empty intersection.  */

  var_vr = get_value_range (var);

  /* We may need to make adjustments when VR_P and VAR_VR are numeric
     ranges or anti-ranges.  */
  if (vr_p->type == VR_VARYING
      || vr_p->type == VR_UNDEFINED
      || var_vr->type == VR_VARYING
      || var_vr->type == VR_UNDEFINED
      || symbolic_range_p (vr_p)
      || symbolic_range_p (var_vr))
    return;

  if (var_vr->type == VR_RANGE && vr_p->type == VR_RANGE)
    {
      /* If the two ranges have a non-empty intersection, we can
	 refine the resulting range.  Since the assert expression
	 creates an equivalency and at the same time it asserts a
	 predicate, we can take the intersection of the two ranges to
	 get better precision.  */
      if (value_ranges_intersect_p (var_vr, vr_p))
	{
	  /* Use the larger of the two minimums.  */
	  if (compare_values (vr_p->min, var_vr->min) == -1)
	    min = var_vr->min;
	  else
	    min = vr_p->min;

	  /* Use the smaller of the two maximums.  */
	  if (compare_values (vr_p->max, var_vr->max) == 1)
	    max = var_vr->max;
	  else
	    max = vr_p->max;

	  set_value_range (vr_p, vr_p->type, min, max, vr_p->equiv);
	}
      else
	{
	  /* The two ranges do not intersect, set the new range to
	     VARYING, because we will not be able to do anything
	     meaningful with it.  */
	  set_value_range_to_varying (vr_p);
	}
    }
  else if ((var_vr->type == VR_RANGE && vr_p->type == VR_ANTI_RANGE)
           || (var_vr->type == VR_ANTI_RANGE && vr_p->type == VR_RANGE))
    {
      /* A range and an anti-range will cancel each other only if
	 their ends are the same.  For instance, in the example above,
	 p_8's range ~[0, 0] and p_6's range [0, 0] are incompatible,
	 so VR_P should be set to VR_VARYING.  */
      if (compare_values (var_vr->min, vr_p->min) == 0
	  && compare_values (var_vr->max, vr_p->max) == 0)
	set_value_range_to_varying (vr_p);
      else
	{
	  tree min, max, anti_min, anti_max, real_min, real_max;
	  int cmp;

	  /* We want to compute the logical AND of the two ranges;
	     there are three cases to consider.


	     1. The VR_ANTI_RANGE range is completely within the
		VR_RANGE and the endpoints of the ranges are
		different.  In that case the resulting range
		should be whichever range is more precise.
		Typically that will be the VR_RANGE.

	     2. The VR_ANTI_RANGE is completely disjoint from
		the VR_RANGE.  In this case the resulting range
		should be the VR_RANGE.

	     3. There is some overlap between the VR_ANTI_RANGE
		and the VR_RANGE.

		3a. If the high limit of the VR_ANTI_RANGE resides
		    within the VR_RANGE, then the result is a new
		    VR_RANGE starting at the high limit of the
		    VR_ANTI_RANGE + 1 and extending to the
		    high limit of the original VR_RANGE.

		3b. If the low limit of the VR_ANTI_RANGE resides
		    within the VR_RANGE, then the result is a new
		    VR_RANGE starting at the low limit of the original
		    VR_RANGE and extending to the low limit of the
		    VR_ANTI_RANGE - 1.  */
	  if (vr_p->type == VR_ANTI_RANGE)
	    {
	      anti_min = vr_p->min;
	      anti_max = vr_p->max;
	      real_min = var_vr->min;
	      real_max = var_vr->max;
	    }
	  else
	    {
	      anti_min = var_vr->min;
	      anti_max = var_vr->max;
	      real_min = vr_p->min;
	      real_max = vr_p->max;
	    }


	  /* Case 1, VR_ANTI_RANGE completely within VR_RANGE,
	     not including any endpoints.  */
	  if (compare_values (anti_max, real_max) == -1
	      && compare_values (anti_min, real_min) == 1)
	    {
	      /* If the range is covering the whole valid range of
		 the type keep the anti-range.  */
	      if (!vrp_val_is_min (real_min)
		  || !vrp_val_is_max (real_max))
	        set_value_range (vr_p, VR_RANGE, real_min,
				 real_max, vr_p->equiv);
	    }
	  /* Case 2, VR_ANTI_RANGE completely disjoint from
	     VR_RANGE.  */
	  else if (compare_values (anti_min, real_max) == 1
		   || compare_values (anti_max, real_min) == -1)
	    {
	      set_value_range (vr_p, VR_RANGE, real_min,
			       real_max, vr_p->equiv);
	    }
	  /* Case 3a, the anti-range extends into the low
	     part of the real range.  Thus creating a new
	     low for the real range.  */
	  else if (((cmp = compare_values (anti_max, real_min)) == 1
		    || cmp == 0)
		   && compare_values (anti_max, real_max) == -1)
	    {
	      gcc_assert (!is_positive_overflow_infinity (anti_max));
	      if (needs_overflow_infinity (TREE_TYPE (anti_max))
		  && vrp_val_is_max (anti_max))
		{
		  if (!supports_overflow_infinity (TREE_TYPE (var_vr->min)))
		    {
		      set_value_range_to_varying (vr_p);
		      return;
		    }
		  min = positive_overflow_infinity (TREE_TYPE (var_vr->min));
		}
	      else if (!POINTER_TYPE_P (TREE_TYPE (var_vr->min)))
		{
		  if (TYPE_PRECISION (TREE_TYPE (var_vr->min)) == 1
		      && !TYPE_UNSIGNED (TREE_TYPE (var_vr->min)))
		    min = fold_build2 (MINUS_EXPR, TREE_TYPE (var_vr->min),
				       anti_max,
				       build_int_cst (TREE_TYPE (var_vr->min),
						      -1));
		  else
		    min = fold_build2 (PLUS_EXPR, TREE_TYPE (var_vr->min),
				       anti_max,
				       build_int_cst (TREE_TYPE (var_vr->min),
						      1));
		}
	      else
		min = fold_build_pointer_plus_hwi (anti_max, 1);
	      max = real_max;
	      set_value_range (vr_p, VR_RANGE, min, max, vr_p->equiv);
	    }
	  /* Case 3b, the anti-range extends into the high
	     part of the real range.  Thus creating a new
	     higher for the real range.  */
	  else if (compare_values (anti_min, real_min) == 1
		   && ((cmp = compare_values (anti_min, real_max)) == -1
		       || cmp == 0))
	    {
	      gcc_assert (!is_negative_overflow_infinity (anti_min));
	      if (needs_overflow_infinity (TREE_TYPE (anti_min))
		  && vrp_val_is_min (anti_min))
		{
		  if (!supports_overflow_infinity (TREE_TYPE (var_vr->min)))
		    {
		      set_value_range_to_varying (vr_p);
		      return;
		    }
		  max = negative_overflow_infinity (TREE_TYPE (var_vr->min));
		}
	      else if (!POINTER_TYPE_P (TREE_TYPE (var_vr->min)))
		{
		  if (TYPE_PRECISION (TREE_TYPE (var_vr->min)) == 1
		      && !TYPE_UNSIGNED (TREE_TYPE (var_vr->min)))
		    max = fold_build2 (PLUS_EXPR, TREE_TYPE (var_vr->min),
				       anti_min,
				       build_int_cst (TREE_TYPE (var_vr->min),
						      -1));
		  else
		    max = fold_build2 (MINUS_EXPR, TREE_TYPE (var_vr->min),
				       anti_min,
				       build_int_cst (TREE_TYPE (var_vr->min),
						      1));
		}
	      else
		max = fold_build_pointer_plus_hwi (anti_min, -1);
	      min = real_min;
	      set_value_range (vr_p, VR_RANGE, min, max, vr_p->equiv);
	    }
	}
    }
}


/* Extract range information from SSA name VAR and store it in VR.  If
   VAR has an interesting range, use it.  Otherwise, create the
   range [VAR, VAR] and return it.  This is useful in situations where
   we may have conditionals testing values of VARYING names.  For
   instance,

   	x_3 = y_5;
	if (x_3 > y_5)
	  ...

    Even if y_5 is deemed VARYING, we can determine that x_3 > y_5 is
    always false.  */

static void
extract_range_from_ssa_name (value_range_t *vr, tree var)
{
  value_range_t *var_vr = get_value_range (var);

  if (var_vr->type != VR_UNDEFINED && var_vr->type != VR_VARYING)
    copy_value_range (vr, var_vr);
  else
    set_value_range (vr, VR_RANGE, var, var, NULL);

  add_equivalence (&vr->equiv, var);
}


/* Wrapper around int_const_binop.  If the operation overflows and we
   are not using wrapping arithmetic, then adjust the result to be
   -INF or +INF depending on CODE, VAL1 and VAL2.  This can return
   NULL_TREE if we need to use an overflow infinity representation but
   the type does not support it.  */

static tree
vrp_int_const_binop (enum tree_code code, tree val1, tree val2)
{
  tree res;

  res = int_const_binop (code, val1, val2);

  /* If we are using unsigned arithmetic, operate symbolically
     on -INF and +INF as int_const_binop only handles signed overflow.  */
  if (TYPE_UNSIGNED (TREE_TYPE (val1)))
    {
      int checkz = compare_values (res, val1);
      bool overflow = false;

      /* Ensure that res = val1 [+*] val2 >= val1
         or that res = val1 - val2 <= val1.  */
      if ((code == PLUS_EXPR
	   && !(checkz == 1 || checkz == 0))
          || (code == MINUS_EXPR
	      && !(checkz == 0 || checkz == -1)))
	{
	  overflow = true;
	}
      /* Checking for multiplication overflow is done by dividing the
	 output of the multiplication by the first input of the
	 multiplication.  If the result of that division operation is
	 not equal to the second input of the multiplication, then the
	 multiplication overflowed.  */
      else if (code == MULT_EXPR && !integer_zerop (val1))
	{
	  tree tmp = int_const_binop (TRUNC_DIV_EXPR,
				      res,
				      val1);
	  int check = compare_values (tmp, val2);

	  if (check != 0)
	    overflow = true;
	}

      if (overflow)
	{
	  res = copy_node (res);
	  TREE_OVERFLOW (res) = 1;
	}

    }
  else if (TYPE_OVERFLOW_WRAPS (TREE_TYPE (val1)))
    /* If the singed operation wraps then int_const_binop has done
       everything we want.  */
    ;
  else if ((TREE_OVERFLOW (res)
	    && !TREE_OVERFLOW (val1)
	    && !TREE_OVERFLOW (val2))
	   || is_overflow_infinity (val1)
	   || is_overflow_infinity (val2))
    {
      /* If the operation overflowed but neither VAL1 nor VAL2 are
	 overflown, return -INF or +INF depending on the operation
	 and the combination of signs of the operands.  */
      int sgn1 = tree_int_cst_sgn (val1);
      int sgn2 = tree_int_cst_sgn (val2);

      if (needs_overflow_infinity (TREE_TYPE (res))
	  && !supports_overflow_infinity (TREE_TYPE (res)))
	return NULL_TREE;

      /* We have to punt on adding infinities of different signs,
	 since we can't tell what the sign of the result should be.
	 Likewise for subtracting infinities of the same sign.  */
      if (((code == PLUS_EXPR && sgn1 != sgn2)
	   || (code == MINUS_EXPR && sgn1 == sgn2))
	  && is_overflow_infinity (val1)
	  && is_overflow_infinity (val2))
	return NULL_TREE;

      /* Don't try to handle division or shifting of infinities.  */
      if ((code == TRUNC_DIV_EXPR
	   || code == FLOOR_DIV_EXPR
	   || code == CEIL_DIV_EXPR
	   || code == EXACT_DIV_EXPR
	   || code == ROUND_DIV_EXPR
	   || code == RSHIFT_EXPR)
	  && (is_overflow_infinity (val1)
	      || is_overflow_infinity (val2)))
	return NULL_TREE;

      /* Notice that we only need to handle the restricted set of
	 operations handled by extract_range_from_binary_expr.
	 Among them, only multiplication, addition and subtraction
	 can yield overflow without overflown operands because we
	 are working with integral types only... except in the
	 case VAL1 = -INF and VAL2 = -1 which overflows to +INF
	 for division too.  */

      /* For multiplication, the sign of the overflow is given
	 by the comparison of the signs of the operands.  */
      if ((code == MULT_EXPR && sgn1 == sgn2)
          /* For addition, the operands must be of the same sign
	     to yield an overflow.  Its sign is therefore that
	     of one of the operands, for example the first.  For
	     infinite operands X + -INF is negative, not positive.  */
	  || (code == PLUS_EXPR
	      && (sgn1 >= 0
		  ? !is_negative_overflow_infinity (val2)
		  : is_positive_overflow_infinity (val2)))
	  /* For subtraction, non-infinite operands must be of
	     different signs to yield an overflow.  Its sign is
	     therefore that of the first operand or the opposite of
	     that of the second operand.  A first operand of 0 counts
	     as positive here, for the corner case 0 - (-INF), which
	     overflows, but must yield +INF.  For infinite operands 0
	     - INF is negative, not positive.  */
	  || (code == MINUS_EXPR
	      && (sgn1 >= 0
		  ? !is_positive_overflow_infinity (val2)
		  : is_negative_overflow_infinity (val2)))
	  /* We only get in here with positive shift count, so the
	     overflow direction is the same as the sign of val1.
	     Actually rshift does not overflow at all, but we only
	     handle the case of shifting overflowed -INF and +INF.  */
	  || (code == RSHIFT_EXPR
	      && sgn1 >= 0)
	  /* For division, the only case is -INF / -1 = +INF.  */
	  || code == TRUNC_DIV_EXPR
	  || code == FLOOR_DIV_EXPR
	  || code == CEIL_DIV_EXPR
	  || code == EXACT_DIV_EXPR
	  || code == ROUND_DIV_EXPR)
	return (needs_overflow_infinity (TREE_TYPE (res))
		? positive_overflow_infinity (TREE_TYPE (res))
		: TYPE_MAX_VALUE (TREE_TYPE (res)));
      else
	return (needs_overflow_infinity (TREE_TYPE (res))
		? negative_overflow_infinity (TREE_TYPE (res))
		: TYPE_MIN_VALUE (TREE_TYPE (res)));
    }

  return res;
}


/* For range VR compute two double_int bitmasks.  In *MAY_BE_NONZERO
   bitmask if some bit is unset, it means for all numbers in the range
   the bit is 0, otherwise it might be 0 or 1.  In *MUST_BE_NONZERO
   bitmask if some bit is set, it means for all numbers in the range
   the bit is 1, otherwise it might be 0 or 1.  */

static bool
zero_nonzero_bits_from_vr (value_range_t *vr,
			   double_int *may_be_nonzero,
			   double_int *must_be_nonzero)
{
  *may_be_nonzero = double_int_minus_one;
  *must_be_nonzero = double_int_zero;
  if (!range_int_cst_p (vr))
    return false;

  if (range_int_cst_singleton_p (vr))
    {
      *may_be_nonzero = tree_to_double_int (vr->min);
      *must_be_nonzero = *may_be_nonzero;
    }
  else if (tree_int_cst_sgn (vr->min) >= 0
	   || tree_int_cst_sgn (vr->max) < 0)
    {
      double_int dmin = tree_to_double_int (vr->min);
      double_int dmax = tree_to_double_int (vr->max);
      double_int xor_mask = double_int_xor (dmin, dmax);
      *may_be_nonzero = double_int_ior (dmin, dmax);
      *must_be_nonzero = double_int_and (dmin, dmax);
      if (xor_mask.high != 0)
	{
	  unsigned HOST_WIDE_INT mask
	      = ((unsigned HOST_WIDE_INT) 1
		 << floor_log2 (xor_mask.high)) - 1;
	  may_be_nonzero->low = ALL_ONES;
	  may_be_nonzero->high |= mask;
	  must_be_nonzero->low = 0;
	  must_be_nonzero->high &= ~mask;
	}
      else if (xor_mask.low != 0)
	{
	  unsigned HOST_WIDE_INT mask
	      = ((unsigned HOST_WIDE_INT) 1
		 << floor_log2 (xor_mask.low)) - 1;
	  may_be_nonzero->low |= mask;
	  must_be_nonzero->low &= ~mask;
	}
    }

  return true;
}

/* Helper to extract a value-range *VR for a multiplicative operation
   *VR0 CODE *VR1.  */

static void
extract_range_from_multiplicative_op_1 (value_range_t *vr,
					enum tree_code code,
					value_range_t *vr0, value_range_t *vr1)
{
  enum value_range_type type;
  tree val[4];
  size_t i;
  tree min, max;
  bool sop;
  int cmp;

  /* Multiplications, divisions and shifts are a bit tricky to handle,
     depending on the mix of signs we have in the two ranges, we
     need to operate on different values to get the minimum and
     maximum values for the new range.  One approach is to figure
     out all the variations of range combinations and do the
     operations.

     However, this involves several calls to compare_values and it
     is pretty convoluted.  It's simpler to do the 4 operations
     (MIN0 OP MIN1, MIN0 OP MAX1, MAX0 OP MIN1 and MAX0 OP MAX0 OP
     MAX1) and then figure the smallest and largest values to form
     the new range.  */
  gcc_assert (code == MULT_EXPR
	      || code == TRUNC_DIV_EXPR
	      || code == FLOOR_DIV_EXPR
	      || code == CEIL_DIV_EXPR
	      || code == EXACT_DIV_EXPR
	      || code == ROUND_DIV_EXPR
	      || code == RSHIFT_EXPR);
  gcc_assert ((vr0->type == VR_RANGE
	       || (code == MULT_EXPR && vr0->type == VR_ANTI_RANGE))
	      && vr0->type == vr1->type);

  type = vr0->type;

  /* Compute the 4 cross operations.  */
  sop = false;
  val[0] = vrp_int_const_binop (code, vr0->min, vr1->min);
  if (val[0] == NULL_TREE)
    sop = true;

  if (vr1->max == vr1->min)
    val[1] = NULL_TREE;
  else
    {
      val[1] = vrp_int_const_binop (code, vr0->min, vr1->max);
      if (val[1] == NULL_TREE)
	sop = true;
    }

  if (vr0->max == vr0->min)
    val[2] = NULL_TREE;
  else
    {
      val[2] = vrp_int_const_binop (code, vr0->max, vr1->min);
      if (val[2] == NULL_TREE)
	sop = true;
    }

  if (vr0->min == vr0->max || vr1->min == vr1->max)
    val[3] = NULL_TREE;
  else
    {
      val[3] = vrp_int_const_binop (code, vr0->max, vr1->max);
      if (val[3] == NULL_TREE)
	sop = true;
    }

  if (sop)
    {
      set_value_range_to_varying (vr);
      return;
    }

  /* Set MIN to the minimum of VAL[i] and MAX to the maximum
     of VAL[i].  */
  min = val[0];
  max = val[0];
  for (i = 1; i < 4; i++)
    {
      if (!is_gimple_min_invariant (min)
	  || (TREE_OVERFLOW (min) && !is_overflow_infinity (min))
	  || !is_gimple_min_invariant (max)
	  || (TREE_OVERFLOW (max) && !is_overflow_infinity (max)))
	break;

      if (val[i])
	{
	  if (!is_gimple_min_invariant (val[i])
	      || (TREE_OVERFLOW (val[i])
		  && !is_overflow_infinity (val[i])))
	    {
	      /* If we found an overflowed value, set MIN and MAX
		 to it so that we set the resulting range to
		 VARYING.  */
	      min = max = val[i];
	      break;
	    }

	  if (compare_values (val[i], min) == -1)
	    min = val[i];

	  if (compare_values (val[i], max) == 1)
	    max = val[i];
	}
    }

  /* If either MIN or MAX overflowed, then set the resulting range to
     VARYING.  But we do accept an overflow infinity
     representation.  */
  if (min == NULL_TREE
      || !is_gimple_min_invariant (min)
      || (TREE_OVERFLOW (min) && !is_overflow_infinity (min))
      || max == NULL_TREE
      || !is_gimple_min_invariant (max)
      || (TREE_OVERFLOW (max) && !is_overflow_infinity (max)))
    {
      set_value_range_to_varying (vr);
      return;
    }

  /* We punt if:
     1) [-INF, +INF]
     2) [-INF, +-INF(OVF)]
     3) [+-INF(OVF), +INF]
     4) [+-INF(OVF), +-INF(OVF)]
     We learn nothing when we have INF and INF(OVF) on both sides.
     Note that we do accept [-INF, -INF] and [+INF, +INF] without
     overflow.  */
  if ((vrp_val_is_min (min) || is_overflow_infinity (min))
      && (vrp_val_is_max (max) || is_overflow_infinity (max)))
    {
      set_value_range_to_varying (vr);
      return;
    }

  cmp = compare_values (min, max);
  if (cmp == -2 || cmp == 1)
    {
      /* If the new range has its limits swapped around (MIN > MAX),
	 then the operation caused one of them to wrap around, mark
	 the new range VARYING.  */
      set_value_range_to_varying (vr);
    }
  else
    set_value_range (vr, type, min, max, NULL);
}

/* Extract range information from a binary operation CODE based on
   the ranges of each of its operands, *VR0 and *VR1 with resulting
   type EXPR_TYPE.  The resulting range is stored in *VR.  */

static void
extract_range_from_binary_expr_1 (value_range_t *vr,
				  enum tree_code code, tree expr_type,
				  value_range_t *vr0_, value_range_t *vr1_)
{
  value_range_t vr0 = *vr0_, vr1 = *vr1_;
  enum value_range_type type;
  tree min = NULL_TREE, max = NULL_TREE;
  int cmp;

  if (!INTEGRAL_TYPE_P (expr_type)
      && !POINTER_TYPE_P (expr_type))
    {
      set_value_range_to_varying (vr);
      return;
    }

  /* Not all binary expressions can be applied to ranges in a
     meaningful way.  Handle only arithmetic operations.  */
  if (code != PLUS_EXPR
      && code != MINUS_EXPR
      && code != POINTER_PLUS_EXPR
      && code != MULT_EXPR
      && code != TRUNC_DIV_EXPR
      && code != FLOOR_DIV_EXPR
      && code != CEIL_DIV_EXPR
      && code != EXACT_DIV_EXPR
      && code != ROUND_DIV_EXPR
      && code != TRUNC_MOD_EXPR
      && code != RSHIFT_EXPR
      && code != MIN_EXPR
      && code != MAX_EXPR
      && code != BIT_AND_EXPR
      && code != BIT_IOR_EXPR
      && code != BIT_XOR_EXPR)
    {
      set_value_range_to_varying (vr);
      return;
    }

  /* If both ranges are UNDEFINED, so is the result.  */
  if (vr0.type == VR_UNDEFINED && vr1.type == VR_UNDEFINED)
    {
      set_value_range_to_undefined (vr);
      return;
    }
  /* If one of the ranges is UNDEFINED drop it to VARYING for the following
     code.  At some point we may want to special-case operations that
     have UNDEFINED result for all or some value-ranges of the not UNDEFINED
     operand.  */
  else if (vr0.type == VR_UNDEFINED)
    set_value_range_to_varying (&vr0);
  else if (vr1.type == VR_UNDEFINED)
    set_value_range_to_varying (&vr1);

  /* The type of the resulting value range defaults to VR0.TYPE.  */
  type = vr0.type;

  /* Refuse to operate on VARYING ranges, ranges of different kinds
     and symbolic ranges.  As an exception, we allow BIT_AND_EXPR
     because we may be able to derive a useful range even if one of
     the operands is VR_VARYING or symbolic range.  Similarly for
     divisions.  TODO, we may be able to derive anti-ranges in
     some cases.  */
  if (code != BIT_AND_EXPR
      && code != BIT_IOR_EXPR
      && code != TRUNC_DIV_EXPR
      && code != FLOOR_DIV_EXPR
      && code != CEIL_DIV_EXPR
      && code != EXACT_DIV_EXPR
      && code != ROUND_DIV_EXPR
      && code != TRUNC_MOD_EXPR
      && (vr0.type == VR_VARYING
	  || vr1.type == VR_VARYING
	  || vr0.type != vr1.type
	  || symbolic_range_p (&vr0)
	  || symbolic_range_p (&vr1)))
    {
      set_value_range_to_varying (vr);
      return;
    }

  /* Now evaluate the expression to determine the new range.  */
  if (POINTER_TYPE_P (expr_type))
    {
      if (code == MIN_EXPR || code == MAX_EXPR)
	{
	  /* For MIN/MAX expressions with pointers, we only care about
	     nullness, if both are non null, then the result is nonnull.
	     If both are null, then the result is null. Otherwise they
	     are varying.  */
	  if (range_is_nonnull (&vr0) && range_is_nonnull (&vr1))
	    set_value_range_to_nonnull (vr, expr_type);
	  else if (range_is_null (&vr0) && range_is_null (&vr1))
	    set_value_range_to_null (vr, expr_type);
	  else
	    set_value_range_to_varying (vr);
	}
      else if (code == POINTER_PLUS_EXPR)
	{
	  /* For pointer types, we are really only interested in asserting
	     whether the expression evaluates to non-NULL.  */
	  if (range_is_nonnull (&vr0) || range_is_nonnull (&vr1))
	    set_value_range_to_nonnull (vr, expr_type);
	  else if (range_is_null (&vr0) && range_is_null (&vr1))
	    set_value_range_to_null (vr, expr_type);
	  else
	    set_value_range_to_varying (vr);
	}
      else if (code == BIT_AND_EXPR)
	{
	  /* For pointer types, we are really only interested in asserting
	     whether the expression evaluates to non-NULL.  */
	  if (range_is_nonnull (&vr0) && range_is_nonnull (&vr1))
	    set_value_range_to_nonnull (vr, expr_type);
	  else if (range_is_null (&vr0) || range_is_null (&vr1))
	    set_value_range_to_null (vr, expr_type);
	  else
	    set_value_range_to_varying (vr);
	}
      else
	set_value_range_to_varying (vr);

      return;
    }

  /* For integer ranges, apply the operation to each end of the
     range and see what we end up with.  */
  if (code == PLUS_EXPR)
    {
      /* If we have a PLUS_EXPR with two VR_ANTI_RANGEs, drop to
	 VR_VARYING.  It would take more effort to compute a precise
	 range for such a case.  For example, if we have op0 == 1 and
	 op1 == -1 with their ranges both being ~[0,0], we would have
	 op0 + op1 == 0, so we cannot claim that the sum is in ~[0,0].
	 Note that we are guaranteed to have vr0.type == vr1.type at
	 this point.  */
      if (vr0.type == VR_ANTI_RANGE)
	{
	  set_value_range_to_varying (vr);
	  return;
	}

      /* For operations that make the resulting range directly
	 proportional to the original ranges, apply the operation to
	 the same end of each range.  */
      min = vrp_int_const_binop (code, vr0.min, vr1.min);
      max = vrp_int_const_binop (code, vr0.max, vr1.max);

      /* If both additions overflowed the range kind is still correct.
	 This happens regularly with subtracting something in unsigned
	 arithmetic.
         ???  See PR30318 for all the cases we do not handle.  */
      if ((TREE_OVERFLOW (min) && !is_overflow_infinity (min))
	  && (TREE_OVERFLOW (max) && !is_overflow_infinity (max)))
	{
	  min = build_int_cst_wide (TREE_TYPE (min),
				    TREE_INT_CST_LOW (min),
				    TREE_INT_CST_HIGH (min));
	  max = build_int_cst_wide (TREE_TYPE (max),
				    TREE_INT_CST_LOW (max),
				    TREE_INT_CST_HIGH (max));
	}
    }
  else if (code == MIN_EXPR
	   || code == MAX_EXPR)
    {
      if (vr0.type == VR_ANTI_RANGE)
	{
	  /* For MIN_EXPR and MAX_EXPR with two VR_ANTI_RANGEs,
	     the resulting VR_ANTI_RANGE is the same - intersection
	     of the two ranges.  */
	  min = vrp_int_const_binop (MAX_EXPR, vr0.min, vr1.min);
	  max = vrp_int_const_binop (MIN_EXPR, vr0.max, vr1.max);
	}
      else
	{
	  /* For operations that make the resulting range directly
	     proportional to the original ranges, apply the operation to
	     the same end of each range.  */
	  min = vrp_int_const_binop (code, vr0.min, vr1.min);
	  max = vrp_int_const_binop (code, vr0.max, vr1.max);
	}
    }
  else if (code == MULT_EXPR)
    {
      /* If we have an unsigned MULT_EXPR with two VR_ANTI_RANGEs,
	 drop to VR_VARYING.  It would take more effort to compute a
	 precise range for such a case.  For example, if we have
	 op0 == 65536 and op1 == 65536 with their ranges both being
	 ~[0,0] on a 32-bit machine, we would have op0 * op1 == 0, so
	 we cannot claim that the product is in ~[0,0].  Note that we
	 are guaranteed to have vr0.type == vr1.type at this
	 point.  */
      if (vr0.type == VR_ANTI_RANGE
	  && !TYPE_OVERFLOW_UNDEFINED (expr_type))
	{
	  set_value_range_to_varying (vr);
	  return;
	}

      extract_range_from_multiplicative_op_1 (vr, code, &vr0, &vr1);
      return;
    }
  else if (code == RSHIFT_EXPR)
    {
      /* If we have a RSHIFT_EXPR with any shift values outside [0..prec-1],
	 then drop to VR_VARYING.  Outside of this range we get undefined
	 behavior from the shift operation.  We cannot even trust
	 SHIFT_COUNT_TRUNCATED at this stage, because that applies to rtl
	 shifts, and the operation at the tree level may be widened.  */
      if (vr1.type != VR_RANGE
	  || !value_range_nonnegative_p (&vr1)
	  || TREE_CODE (vr1.max) != INTEGER_CST
	  || compare_tree_int (vr1.max, TYPE_PRECISION (expr_type) - 1) == 1)
	{
	  set_value_range_to_varying (vr);
	  return;
	}

      extract_range_from_multiplicative_op_1 (vr, code, &vr0, &vr1);
      return;
    }
  else if (code == TRUNC_DIV_EXPR
	   || code == FLOOR_DIV_EXPR
	   || code == CEIL_DIV_EXPR
	   || code == EXACT_DIV_EXPR
	   || code == ROUND_DIV_EXPR)
    {
      if (vr0.type != VR_RANGE || symbolic_range_p (&vr0))
	{
	  /* For division, if op1 has VR_RANGE but op0 does not, something
	     can be deduced just from that range.  Say [min, max] / [4, max]
	     gives [min / 4, max / 4] range.  */
	  if (vr1.type == VR_RANGE
	      && !symbolic_range_p (&vr1)
	      && !range_includes_zero_p (&vr1))
	    {
	      vr0.type = type = VR_RANGE;
	      vr0.min = vrp_val_min (expr_type);
	      vr0.max = vrp_val_max (expr_type);
	    }
	  else
	    {
	      set_value_range_to_varying (vr);
	      return;
	    }
	}

      /* For divisions, if flag_non_call_exceptions is true, we must
	 not eliminate a division by zero.  */
      if (cfun->can_throw_non_call_exceptions
	  && (vr1.type != VR_RANGE
	      || symbolic_range_p (&vr1)
	      || range_includes_zero_p (&vr1)))
	{
	  set_value_range_to_varying (vr);
	  return;
	}

      /* For divisions, if op0 is VR_RANGE, we can deduce a range
	 even if op1 is VR_VARYING, VR_ANTI_RANGE, symbolic or can
	 include 0.  */
      if (vr0.type == VR_RANGE
	  && (vr1.type != VR_RANGE
	      || symbolic_range_p (&vr1)
	      || range_includes_zero_p (&vr1)))
	{
	  tree zero = build_int_cst (TREE_TYPE (vr0.min), 0);
	  int cmp;

	  min = NULL_TREE;
	  max = NULL_TREE;
	  if (TYPE_UNSIGNED (expr_type)
	      || value_range_nonnegative_p (&vr1))
	    {
	      /* For unsigned division or when divisor is known
		 to be non-negative, the range has to cover
		 all numbers from 0 to max for positive max
		 and all numbers from min to 0 for negative min.  */
	      cmp = compare_values (vr0.max, zero);
	      if (cmp == -1)
		max = zero;
	      else if (cmp == 0 || cmp == 1)
		max = vr0.max;
	      else
		type = VR_VARYING;
	      cmp = compare_values (vr0.min, zero);
	      if (cmp == 1)
		min = zero;
	      else if (cmp == 0 || cmp == -1)
		min = vr0.min;
	      else
		type = VR_VARYING;
	    }
	  else
	    {
	      /* Otherwise the range is -max .. max or min .. -min
		 depending on which bound is bigger in absolute value,
		 as the division can change the sign.  */
	      abs_extent_range (vr, vr0.min, vr0.max);
	      return;
	    }
	  if (type == VR_VARYING)
	    {
	      set_value_range_to_varying (vr);
	      return;
	    }
	}
      else
	{
	  extract_range_from_multiplicative_op_1 (vr, code, &vr0, &vr1);
	  return;
	}
    }
  else if (code == TRUNC_MOD_EXPR)
    {
      if (vr1.type != VR_RANGE
	  || symbolic_range_p (&vr1)
	  || range_includes_zero_p (&vr1)
	  || vrp_val_is_min (vr1.min))
	{
	  set_value_range_to_varying (vr);
	  return;
	}
      type = VR_RANGE;
      /* Compute MAX <|vr1.min|, |vr1.max|> - 1.  */
      max = fold_unary_to_constant (ABS_EXPR, expr_type, vr1.min);
      if (tree_int_cst_lt (max, vr1.max))
	max = vr1.max;
      max = int_const_binop (MINUS_EXPR, max, integer_one_node);
      /* If the dividend is non-negative the modulus will be
	 non-negative as well.  */
      if (TYPE_UNSIGNED (expr_type)
	  || value_range_nonnegative_p (&vr0))
	min = build_int_cst (TREE_TYPE (max), 0);
      else
	min = fold_unary_to_constant (NEGATE_EXPR, expr_type, max);
    }
  else if (code == MINUS_EXPR)
    {
      /* If we have a MINUS_EXPR with two VR_ANTI_RANGEs, drop to
	 VR_VARYING.  It would take more effort to compute a precise
	 range for such a case.  For example, if we have op0 == 1 and
	 op1 == 1 with their ranges both being ~[0,0], we would have
	 op0 - op1 == 0, so we cannot claim that the difference is in
	 ~[0,0].  Note that we are guaranteed to have
	 vr0.type == vr1.type at this point.  */
      if (vr0.type == VR_ANTI_RANGE)
	{
	  set_value_range_to_varying (vr);
	  return;
	}

      /* For MINUS_EXPR, apply the operation to the opposite ends of
	 each range.  */
      min = vrp_int_const_binop (code, vr0.min, vr1.max);
      max = vrp_int_const_binop (code, vr0.max, vr1.min);
    }
  else if (code == BIT_AND_EXPR || code == BIT_IOR_EXPR || code == BIT_XOR_EXPR)
    {
      bool int_cst_range0, int_cst_range1;
      double_int may_be_nonzero0, may_be_nonzero1;
      double_int must_be_nonzero0, must_be_nonzero1;

      int_cst_range0 = zero_nonzero_bits_from_vr (&vr0, &may_be_nonzero0,
						  &must_be_nonzero0);
      int_cst_range1 = zero_nonzero_bits_from_vr (&vr1, &may_be_nonzero1,
						  &must_be_nonzero1);

      type = VR_RANGE;
      if (code == BIT_AND_EXPR)
	{
	  double_int dmax;
	  min = double_int_to_tree (expr_type,
				    double_int_and (must_be_nonzero0,
						    must_be_nonzero1));
	  dmax = double_int_and (may_be_nonzero0, may_be_nonzero1);
	  /* If both input ranges contain only negative values we can
	     truncate the result range maximum to the minimum of the
	     input range maxima.  */
	  if (int_cst_range0 && int_cst_range1
	      && tree_int_cst_sgn (vr0.max) < 0
	      && tree_int_cst_sgn (vr1.max) < 0)
	    {
	      dmax = double_int_min (dmax, tree_to_double_int (vr0.max),
				     TYPE_UNSIGNED (expr_type));
	      dmax = double_int_min (dmax, tree_to_double_int (vr1.max),
				     TYPE_UNSIGNED (expr_type));
	    }
	  /* If either input range contains only non-negative values
	     we can truncate the result range maximum to the respective
	     maximum of the input range.  */
	  if (int_cst_range0 && tree_int_cst_sgn (vr0.min) >= 0)
	    dmax = double_int_min (dmax, tree_to_double_int (vr0.max),
				   TYPE_UNSIGNED (expr_type));
	  if (int_cst_range1 && tree_int_cst_sgn (vr1.min) >= 0)
	    dmax = double_int_min (dmax, tree_to_double_int (vr1.max),
				   TYPE_UNSIGNED (expr_type));
	  max = double_int_to_tree (expr_type, dmax);
	}
      else if (code == BIT_IOR_EXPR)
	{
	  double_int dmin;
	  max = double_int_to_tree (expr_type,
				    double_int_ior (may_be_nonzero0,
						    may_be_nonzero1));
	  dmin = double_int_ior (must_be_nonzero0, must_be_nonzero1);
	  /* If the input ranges contain only positive values we can
	     truncate the minimum of the result range to the maximum
	     of the input range minima.  */
	  if (int_cst_range0 && int_cst_range1
	      && tree_int_cst_sgn (vr0.min) >= 0
	      && tree_int_cst_sgn (vr1.min) >= 0)
	    {
	      dmin = double_int_max (dmin, tree_to_double_int (vr0.min),
				     TYPE_UNSIGNED (expr_type));
	      dmin = double_int_max (dmin, tree_to_double_int (vr1.min),
				     TYPE_UNSIGNED (expr_type));
	    }
	  /* If either input range contains only negative values
	     we can truncate the minimum of the result range to the
	     respective minimum range.  */
	  if (int_cst_range0 && tree_int_cst_sgn (vr0.max) < 0)
	    dmin = double_int_max (dmin, tree_to_double_int (vr0.min),
				   TYPE_UNSIGNED (expr_type));
	  if (int_cst_range1 && tree_int_cst_sgn (vr1.max) < 0)
	    dmin = double_int_max (dmin, tree_to_double_int (vr1.min),
				   TYPE_UNSIGNED (expr_type));
	  min = double_int_to_tree (expr_type, dmin);
	}
      else if (code == BIT_XOR_EXPR)
	{
	  double_int result_zero_bits, result_one_bits;
	  result_zero_bits
	    = double_int_ior (double_int_and (must_be_nonzero0,
					      must_be_nonzero1),
			      double_int_not
			        (double_int_ior (may_be_nonzero0,
						 may_be_nonzero1)));
	  result_one_bits
	    = double_int_ior (double_int_and
			        (must_be_nonzero0,
				 double_int_not (may_be_nonzero1)),
			      double_int_and
			        (must_be_nonzero1,
				 double_int_not (may_be_nonzero0)));
	  max = double_int_to_tree (expr_type,
				    double_int_not (result_zero_bits));
	  min = double_int_to_tree (expr_type, result_one_bits);
	  /* If the range has all positive or all negative values the
	     result is better than VARYING.  */
	  if (tree_int_cst_sgn (min) < 0
	      || tree_int_cst_sgn (max) >= 0)
	    ;
	  else
	    max = min = NULL_TREE;
	}
    }
  else
    gcc_unreachable ();

  /* If either MIN or MAX overflowed, then set the resulting range to
     VARYING.  But we do accept an overflow infinity
     representation.  */
  if (min == NULL_TREE
      || !is_gimple_min_invariant (min)
      || (TREE_OVERFLOW (min) && !is_overflow_infinity (min))
      || max == NULL_TREE
      || !is_gimple_min_invariant (max)
      || (TREE_OVERFLOW (max) && !is_overflow_infinity (max)))
    {
      set_value_range_to_varying (vr);
      return;
    }

  /* We punt if:
     1) [-INF, +INF]
     2) [-INF, +-INF(OVF)]
     3) [+-INF(OVF), +INF]
     4) [+-INF(OVF), +-INF(OVF)]
     We learn nothing when we have INF and INF(OVF) on both sides.
     Note that we do accept [-INF, -INF] and [+INF, +INF] without
     overflow.  */
  if ((vrp_val_is_min (min) || is_overflow_infinity (min))
      && (vrp_val_is_max (max) || is_overflow_infinity (max)))
    {
      set_value_range_to_varying (vr);
      return;
    }

  cmp = compare_values (min, max);
  if (cmp == -2 || cmp == 1)
    {
      /* If the new range has its limits swapped around (MIN > MAX),
	 then the operation caused one of them to wrap around, mark
	 the new range VARYING.  */
      set_value_range_to_varying (vr);
    }
  else
    set_value_range (vr, type, min, max, NULL);
}

/* Extract range information from a binary expression OP0 CODE OP1 based on
   the ranges of each of its operands with resulting type EXPR_TYPE.
   The resulting range is stored in *VR.  */

static void
extract_range_from_binary_expr (value_range_t *vr,
				enum tree_code code,
				tree expr_type, tree op0, tree op1)
{
  value_range_t vr0 = { VR_UNDEFINED, NULL_TREE, NULL_TREE, NULL };
  value_range_t vr1 = { VR_UNDEFINED, NULL_TREE, NULL_TREE, NULL };

  /* Get value ranges for each operand.  For constant operands, create
     a new value range with the operand to simplify processing.  */
  if (TREE_CODE (op0) == SSA_NAME)
    vr0 = *(get_value_range (op0));
  else if (is_gimple_min_invariant (op0))
    set_value_range_to_value (&vr0, op0, NULL);
  else
    set_value_range_to_varying (&vr0);

  if (TREE_CODE (op1) == SSA_NAME)
    vr1 = *(get_value_range (op1));
  else if (is_gimple_min_invariant (op1))
    set_value_range_to_value (&vr1, op1, NULL);
  else
    set_value_range_to_varying (&vr1);

  extract_range_from_binary_expr_1 (vr, code, expr_type, &vr0, &vr1);
}

/* Extract range information from a unary operation CODE based on
   the range of its operand *VR0 with type OP0_TYPE with resulting type TYPE.
   The The resulting range is stored in *VR.  */

static void
extract_range_from_unary_expr_1 (value_range_t *vr,
				 enum tree_code code, tree type,
				 value_range_t *vr0_, tree op0_type)
{
  value_range_t vr0 = *vr0_;

  /* VRP only operates on integral and pointer types.  */
  if (!(INTEGRAL_TYPE_P (op0_type)
	|| POINTER_TYPE_P (op0_type))
      || !(INTEGRAL_TYPE_P (type)
	   || POINTER_TYPE_P (type)))
    {
      set_value_range_to_varying (vr);
      return;
    }

  /* If VR0 is UNDEFINED, so is the result.  */
  if (vr0.type == VR_UNDEFINED)
    {
      set_value_range_to_undefined (vr);
      return;
    }

  if (CONVERT_EXPR_CODE_P (code))
    {
      tree inner_type = op0_type;
      tree outer_type = type;

      /* If the expression evaluates to a pointer, we are only interested in
	 determining if it evaluates to NULL [0, 0] or non-NULL (~[0, 0]).  */
      if (POINTER_TYPE_P (type))
	{
	  if (range_is_nonnull (&vr0))
	    set_value_range_to_nonnull (vr, type);
	  else if (range_is_null (&vr0))
	    set_value_range_to_null (vr, type);
	  else
	    set_value_range_to_varying (vr);
	  return;
	}

      /* If VR0 is varying and we increase the type precision, assume
	 a full range for the following transformation.  */
      if (vr0.type == VR_VARYING
	  && INTEGRAL_TYPE_P (inner_type)
	  && TYPE_PRECISION (inner_type) < TYPE_PRECISION (outer_type))
	{
	  vr0.type = VR_RANGE;
	  vr0.min = TYPE_MIN_VALUE (inner_type);
	  vr0.max = TYPE_MAX_VALUE (inner_type);
	}

      /* If VR0 is a constant range or anti-range and the conversion is
	 not truncating we can convert the min and max values and
	 canonicalize the resulting range.  Otherwise we can do the
	 conversion if the size of the range is less than what the
	 precision of the target type can represent and the range is
	 not an anti-range.  */
      if ((vr0.type == VR_RANGE
	   || vr0.type == VR_ANTI_RANGE)
	  && TREE_CODE (vr0.min) == INTEGER_CST
	  && TREE_CODE (vr0.max) == INTEGER_CST
	  && (!is_overflow_infinity (vr0.min)
	      || (vr0.type == VR_RANGE
		  && TYPE_PRECISION (outer_type) > TYPE_PRECISION (inner_type)
		  && needs_overflow_infinity (outer_type)
		  && supports_overflow_infinity (outer_type)))
	  && (!is_overflow_infinity (vr0.max)
	      || (vr0.type == VR_RANGE
		  && TYPE_PRECISION (outer_type) > TYPE_PRECISION (inner_type)
		  && needs_overflow_infinity (outer_type)
		  && supports_overflow_infinity (outer_type)))
	  && (TYPE_PRECISION (outer_type) >= TYPE_PRECISION (inner_type)
	      || (vr0.type == VR_RANGE
		  && integer_zerop (int_const_binop (RSHIFT_EXPR,
		       int_const_binop (MINUS_EXPR, vr0.max, vr0.min),
		         size_int (TYPE_PRECISION (outer_type)))))))
	{
	  tree new_min, new_max;
	  if (is_overflow_infinity (vr0.min))
	    new_min = negative_overflow_infinity (outer_type);
	  else
	    new_min = force_fit_type_double (outer_type,
					     tree_to_double_int (vr0.min),
					     0, false);
	  if (is_overflow_infinity (vr0.max))
	    new_max = positive_overflow_infinity (outer_type);
	  else
	    new_max = force_fit_type_double (outer_type,
					     tree_to_double_int (vr0.max),
					     0, false);
	  set_and_canonicalize_value_range (vr, vr0.type,
					    new_min, new_max, NULL);
	  return;
	}

      set_value_range_to_varying (vr);
      return;
    }
  else if (code == NEGATE_EXPR)
    {
      /* -X is simply 0 - X, so re-use existing code that also handles
         anti-ranges fine.  */
      value_range_t zero = { VR_UNDEFINED, NULL_TREE, NULL_TREE, NULL };
      set_value_range_to_value (&zero, build_int_cst (type, 0), NULL);
      extract_range_from_binary_expr_1 (vr, MINUS_EXPR, type, &zero, &vr0);
      return;
    }
  else if (code == ABS_EXPR)
    {
      tree min, max;
      int cmp;

      /* Pass through vr0 in the easy cases.  */
      if (TYPE_UNSIGNED (type)
	  || value_range_nonnegative_p (&vr0))
	{
	  copy_value_range (vr, &vr0);
	  return;
	}

      /* For the remaining varying or symbolic ranges we can't do anything
	 useful.  */
      if (vr0.type == VR_VARYING
	  || symbolic_range_p (&vr0))
	{
	  set_value_range_to_varying (vr);
	  return;
	}

      /* -TYPE_MIN_VALUE = TYPE_MIN_VALUE with flag_wrapv so we can't get a
         useful range.  */
      if (!TYPE_OVERFLOW_UNDEFINED (type)
	  && ((vr0.type == VR_RANGE
	       && vrp_val_is_min (vr0.min))
	      || (vr0.type == VR_ANTI_RANGE
		  && !vrp_val_is_min (vr0.min))))
	{
	  set_value_range_to_varying (vr);
	  return;
	}

      /* ABS_EXPR may flip the range around, if the original range
	 included negative values.  */
      if (is_overflow_infinity (vr0.min))
	min = positive_overflow_infinity (type);
      else if (!vrp_val_is_min (vr0.min))
	min = fold_unary_to_constant (code, type, vr0.min);
      else if (!needs_overflow_infinity (type))
	min = TYPE_MAX_VALUE (type);
      else if (supports_overflow_infinity (type))
	min = positive_overflow_infinity (type);
      else
	{
	  set_value_range_to_varying (vr);
	  return;
	}

      if (is_overflow_infinity (vr0.max))
	max = positive_overflow_infinity (type);
      else if (!vrp_val_is_min (vr0.max))
	max = fold_unary_to_constant (code, type, vr0.max);
      else if (!needs_overflow_infinity (type))
	max = TYPE_MAX_VALUE (type);
      else if (supports_overflow_infinity (type)
	       /* We shouldn't generate [+INF, +INF] as set_value_range
		  doesn't like this and ICEs.  */
	       && !is_positive_overflow_infinity (min))
	max = positive_overflow_infinity (type);
      else
	{
	  set_value_range_to_varying (vr);
	  return;
	}

      cmp = compare_values (min, max);

      /* If a VR_ANTI_RANGEs contains zero, then we have
	 ~[-INF, min(MIN, MAX)].  */
      if (vr0.type == VR_ANTI_RANGE)
	{
	  if (range_includes_zero_p (&vr0))
	    {
	      /* Take the lower of the two values.  */
	      if (cmp != 1)
		max = min;

	      /* Create ~[-INF, min (abs(MIN), abs(MAX))]
	         or ~[-INF + 1, min (abs(MIN), abs(MAX))] when
		 flag_wrapv is set and the original anti-range doesn't include
	         TYPE_MIN_VALUE, remember -TYPE_MIN_VALUE = TYPE_MIN_VALUE.  */
	      if (TYPE_OVERFLOW_WRAPS (type))
		{
		  tree type_min_value = TYPE_MIN_VALUE (type);

		  min = (vr0.min != type_min_value
			 ? int_const_binop (PLUS_EXPR, type_min_value,
					    integer_one_node)
			 : type_min_value);
		}
	      else
		{
		  if (overflow_infinity_range_p (&vr0))
		    min = negative_overflow_infinity (type);
		  else
		    min = TYPE_MIN_VALUE (type);
		}
	    }
	  else
	    {
	      /* All else has failed, so create the range [0, INF], even for
	         flag_wrapv since TYPE_MIN_VALUE is in the original
	         anti-range.  */
	      vr0.type = VR_RANGE;
	      min = build_int_cst (type, 0);
	      if (needs_overflow_infinity (type))
		{
		  if (supports_overflow_infinity (type))
		    max = positive_overflow_infinity (type);
		  else
		    {
		      set_value_range_to_varying (vr);
		      return;
		    }
		}
	      else
		max = TYPE_MAX_VALUE (type);
	    }
	}

      /* If the range contains zero then we know that the minimum value in the
         range will be zero.  */
      else if (range_includes_zero_p (&vr0))
	{
	  if (cmp == 1)
	    max = min;
	  min = build_int_cst (type, 0);
	}
      else
	{
          /* If the range was reversed, swap MIN and MAX.  */
	  if (cmp == 1)
	    {
	      tree t = min;
	      min = max;
	      max = t;
	    }
	}

      cmp = compare_values (min, max);
      if (cmp == -2 || cmp == 1)
	{
	  /* If the new range has its limits swapped around (MIN > MAX),
	     then the operation caused one of them to wrap around, mark
	     the new range VARYING.  */
	  set_value_range_to_varying (vr);
	}
      else
	set_value_range (vr, vr0.type, min, max, NULL);
      return;
    }
  else if (code == BIT_NOT_EXPR)
    {
      /* ~X is simply -1 - X, so re-use existing code that also handles
         anti-ranges fine.  */
      value_range_t minusone = { VR_UNDEFINED, NULL_TREE, NULL_TREE, NULL };
      set_value_range_to_value (&minusone, build_int_cst (type, -1), NULL);
      extract_range_from_binary_expr_1 (vr, MINUS_EXPR,
					type, &minusone, &vr0);
      return;
    }
  else if (code == PAREN_EXPR)
    {
      copy_value_range (vr, &vr0);
      return;
    }

  /* For unhandled operations fall back to varying.  */
  set_value_range_to_varying (vr);
  return;
}


/* Extract range information from a unary expression CODE OP0 based on
   the range of its operand with resulting type TYPE.
   The resulting range is stored in *VR.  */

static void
extract_range_from_unary_expr (value_range_t *vr, enum tree_code code,
			       tree type, tree op0)
{
  value_range_t vr0 = { VR_UNDEFINED, NULL_TREE, NULL_TREE, NULL };

  /* Get value ranges for the operand.  For constant operands, create
     a new value range with the operand to simplify processing.  */
  if (TREE_CODE (op0) == SSA_NAME)
    vr0 = *(get_value_range (op0));
  else if (is_gimple_min_invariant (op0))
    set_value_range_to_value (&vr0, op0, NULL);
  else
    set_value_range_to_varying (&vr0);

  extract_range_from_unary_expr_1 (vr, code, type, &vr0, TREE_TYPE (op0));
}


/* Extract range information from a conditional expression STMT based on
   the ranges of each of its operands and the expression code.  */

static void
extract_range_from_cond_expr (value_range_t *vr, GIMPLE_type stmt)
{
  tree op0, op1;
  value_range_t vr0 = { VR_UNDEFINED, NULL_TREE, NULL_TREE, NULL };
  value_range_t vr1 = { VR_UNDEFINED, NULL_TREE, NULL_TREE, NULL };

  /* Get value ranges for each operand.  For constant operands, create
     a new value range with the operand to simplify processing.  */
  op0 = gimple_assign_rhs2 (stmt);
  if (TREE_CODE (op0) == SSA_NAME)
    vr0 = *(get_value_range (op0));
  else if (is_gimple_min_invariant (op0))
    set_value_range_to_value (&vr0, op0, NULL);
  else
    set_value_range_to_varying (&vr0);

  op1 = gimple_assign_rhs3 (stmt);
  if (TREE_CODE (op1) == SSA_NAME)
    vr1 = *(get_value_range (op1));
  else if (is_gimple_min_invariant (op1))
    set_value_range_to_value (&vr1, op1, NULL);
  else
    set_value_range_to_varying (&vr1);

  /* The resulting value range is the union of the operand ranges */
  copy_value_range (vr, &vr0);
  vrp_meet (vr, &vr1);
}


/* Extract range information from a comparison expression EXPR based
   on the range of its operand and the expression code.  */

static void
extract_range_from_comparison (value_range_t *vr, enum tree_code code,
			       tree type, tree op0, tree op1)
{
  bool sop = false;
  tree val;

  val = vrp_evaluate_conditional_warnv_with_ops (code, op0, op1, false, &sop,
  						 NULL);

  /* A disadvantage of using a special infinity as an overflow
     representation is that we lose the ability to record overflow
     when we don't have an infinity.  So we have to ignore a result
     which relies on overflow.  */

  if (val && !is_overflow_infinity (val) && !sop)
    {
      /* Since this expression was found on the RHS of an assignment,
	 its type may be different from _Bool.  Convert VAL to EXPR's
	 type.  */
      val = fold_convert (type, val);
      if (is_gimple_min_invariant (val))
	set_value_range_to_value (vr, val, vr->equiv);
      else
	set_value_range (vr, VR_RANGE, val, val, vr->equiv);
    }
  else
    /* The result of a comparison is always true or false.  */
    set_value_range_to_truthvalue (vr, type);
}

/* Try to derive a nonnegative or nonzero range out of STMT relying
   primarily on generic routines in fold in conjunction with range data.
   Store the result in *VR */

static void
extract_range_basic (value_range_t *vr, GIMPLE_type stmt)
{
  bool sop = false;
  tree type = gimple_expr_type (stmt);

  if (INTEGRAL_TYPE_P (type)
      && gimple_stmt_nonnegative_warnv_p (stmt, &sop))
    set_value_range_to_nonnegative (vr, type,
				    sop || stmt_overflow_infinity (stmt));
  else if (vrp_stmt_computes_nonzero (stmt, &sop)
	   && !sop)
    set_value_range_to_nonnull (vr, type);
  else
    set_value_range_to_varying (vr);
}


/* Try to compute a useful range out of assignment STMT and store it
   in *VR.  */

static void
extract_range_from_assignment (value_range_t *vr, GIMPLE_type stmt)
{
  enum tree_code code = gimple_assign_rhs_code (stmt);

  if (code == ASSERT_EXPR)
    extract_range_from_assert (vr, gimple_assign_rhs1 (stmt));
  else if (code == SSA_NAME)
    extract_range_from_ssa_name (vr, gimple_assign_rhs1 (stmt));
  else if (TREE_CODE_CLASS (code) == tcc_binary)
    extract_range_from_binary_expr (vr, gimple_assign_rhs_code (stmt),
				    gimple_expr_type (stmt),
				    gimple_assign_rhs1 (stmt),
				    gimple_assign_rhs2 (stmt));
  else if (TREE_CODE_CLASS (code) == tcc_unary)
    extract_range_from_unary_expr (vr, gimple_assign_rhs_code (stmt),
				   gimple_expr_type (stmt),
				   gimple_assign_rhs1 (stmt));
  else if (code == COND_EXPR)
    extract_range_from_cond_expr (vr, stmt);
  else if (TREE_CODE_CLASS (code) == tcc_comparison)
    extract_range_from_comparison (vr, gimple_assign_rhs_code (stmt),
				   gimple_expr_type (stmt),
				   gimple_assign_rhs1 (stmt),
				   gimple_assign_rhs2 (stmt));
  else if (get_gimple_rhs_class (code) == GIMPLE_SINGLE_RHS
	   && is_gimple_min_invariant (gimple_assign_rhs1 (stmt)))
    set_value_range_to_value (vr, gimple_assign_rhs1 (stmt), NULL);
  else
    set_value_range_to_varying (vr);

  if (vr->type == VR_VARYING)
    extract_range_basic (vr, stmt);
}

/* Given a range VR, a LOOP and a variable VAR, determine whether it
   would be profitable to adjust VR using scalar evolution information
   for VAR.  If so, update VR with the new limits.  */

static void
adjust_range_with_scev (value_range_t *vr, struct loop *loop,
         GIMPLE_type stmt, tree var)
{
  tree init, step, chrec, tmin, tmax, min, max, type, tem;
  enum ev_direction dir;

  /* TODO.  Don't adjust anti-ranges.  An anti-range may provide
     better opportunities than a regular range, but I'm not sure.  */
  if (vr->type == VR_ANTI_RANGE)
    return;

  chrec = instantiate_parameters (loop, analyze_scalar_evolution (loop, var));

  /* Like in PR19590, scev can return a constant function.  */
  if (is_gimple_min_invariant (chrec))
    {
      set_value_range_to_value (vr, chrec, vr->equiv);
      return;
    }

  if (TREE_CODE (chrec) != POLYNOMIAL_CHREC)
    return;

  init = initial_condition_in_loop_num (chrec, loop->num);
  tem = op_with_constant_singleton_value_range (init);
  if (tem)
    init = tem;
  step = evolution_part_in_loop_num (chrec, loop->num);
  tem = op_with_constant_singleton_value_range (step);
  if (tem)
    step = tem;

  /* If STEP is symbolic, we can't know whether INIT will be the
     minimum or maximum value in the range.  Also, unless INIT is
     a simple expression, compare_values and possibly other functions
     in tree-vrp won't be able to handle it.  */
  if (step == NULL_TREE
      || !is_gimple_min_invariant (step)
      || !valid_value_p (init))
    return;

  dir = scev_direction (chrec);
  if (/* Do not adjust ranges if we do not know whether the iv increases
	 or decreases,  ... */
      dir == EV_DIR_UNKNOWN
      /* ... or if it may wrap.  */
      || scev_probably_wraps_p (init, step, stmt, get_chrec_loop (chrec),
				true))
    return;

  /* We use TYPE_MIN_VALUE and TYPE_MAX_VALUE here instead of
     negative_overflow_infinity and positive_overflow_infinity,
     because we have concluded that the loop probably does not
     wrap.  */

  type = TREE_TYPE (var);
  if (POINTER_TYPE_P (type) || !TYPE_MIN_VALUE (type))
    tmin = lower_bound_in_type (type, type);
  else
    tmin = TYPE_MIN_VALUE (type);
  if (POINTER_TYPE_P (type) || !TYPE_MAX_VALUE (type))
    tmax = upper_bound_in_type (type, type);
  else
    tmax = TYPE_MAX_VALUE (type);

  /* Try to use estimated number of iterations for the loop to constrain the
     final value in the evolution.  */
  if (TREE_CODE (step) == INTEGER_CST
      && is_gimple_val (init)
      && (TREE_CODE (init) != SSA_NAME
	  || get_value_range (init)->type == VR_RANGE))
    {
      double_int nit;

      if (estimated_loop_iterations (loop, true, &nit))
	{
	  value_range_t maxvr = { VR_UNDEFINED, NULL_TREE, NULL_TREE, NULL };
	  double_int dtmp;
	  bool unsigned_p = TYPE_UNSIGNED (TREE_TYPE (step));
	  int overflow = 0;

	  dtmp = double_int_mul_with_sign (tree_to_double_int (step), nit,
					   unsigned_p, &overflow);
	  /* If the multiplication overflowed we can't do a meaningful
	     adjustment.  Likewise if the result doesn't fit in the type
	     of the induction variable.  For a signed type we have to
	     check whether the result has the expected signedness which
	     is that of the step as number of iterations is unsigned.  */
	  if (!overflow
	      && double_int_fits_to_tree_p (TREE_TYPE (init), dtmp)
	      && (unsigned_p
		  || ((dtmp.high ^ TREE_INT_CST_HIGH (step)) >= 0)))
	    {
	      tem = double_int_to_tree (TREE_TYPE (init), dtmp);
	      extract_range_from_binary_expr (&maxvr, PLUS_EXPR,
					      TREE_TYPE (init), init, tem);
	      /* Likewise if the addition did.  */
	      if (maxvr.type == VR_RANGE)
		{
		  tmin = maxvr.min;
		  tmax = maxvr.max;
		}
	    }
	}
    }

  if (vr->type == VR_VARYING || vr->type == VR_UNDEFINED)
    {
      min = tmin;
      max = tmax;

      /* For VARYING or UNDEFINED ranges, just about anything we get
	 from scalar evolutions should be better.  */

      if (dir == EV_DIR_DECREASES)
	max = init;
      else
	min = init;

      /* If we would create an invalid range, then just assume we
	 know absolutely nothing.  This may be over-conservative,
	 but it's clearly safe, and should happen only in unreachable
         parts of code, or for invalid programs.  */
      if (compare_values (min, max) == 1)
	return;

      set_value_range (vr, VR_RANGE, min, max, vr->equiv);
    }
  else if (vr->type == VR_RANGE)
    {
      min = vr->min;
      max = vr->max;

      if (dir == EV_DIR_DECREASES)
	{
	  /* INIT is the maximum value.  If INIT is lower than VR->MAX
	     but no smaller than VR->MIN, set VR->MAX to INIT.  */
	  if (compare_values (init, max) == -1)
	    max = init;

	  /* According to the loop information, the variable does not
	     overflow.  If we think it does, probably because of an
	     overflow due to arithmetic on a different INF value,
	     reset now.  */
	  if (is_negative_overflow_infinity (min)
	      || compare_values (min, tmin) == -1)
	    min = tmin;

	}
      else
	{
	  /* If INIT is bigger than VR->MIN, set VR->MIN to INIT.  */
	  if (compare_values (init, min) == 1)
	    min = init;

	  if (is_positive_overflow_infinity (max)
	      || compare_values (tmax, max) == -1)
	    max = tmax;
	}

      /* If we just created an invalid range with the minimum
	 greater than the maximum, we fail conservatively.
	 This should happen only in unreachable
	 parts of code, or for invalid programs.  */
      if (compare_values (min, max) == 1)
	return;

      set_value_range (vr, VR_RANGE, min, max, vr->equiv);
    }
}

/* Return true if VAR may overflow at STMT.  This checks any available
   loop information to see if we can determine that VAR does not
   overflow.  */

static bool
vrp_var_may_overflow (tree var, GIMPLE_type stmt)
{
  struct loop *l;
  tree chrec, init, step;

  if (current_loops == NULL)
    return true;

  l = loop_containing_stmt (stmt);
  if (l == NULL
      || !loop_outer (l))
    return true;

  chrec = instantiate_parameters (l, analyze_scalar_evolution (l, var));
  if (TREE_CODE (chrec) != POLYNOMIAL_CHREC)
    return true;

  init = initial_condition_in_loop_num (chrec, l->num);
  step = evolution_part_in_loop_num (chrec, l->num);

  if (step == NULL_TREE
      || !is_gimple_min_invariant (step)
      || !valid_value_p (init))
    return true;

  /* If we get here, we know something useful about VAR based on the
     loop information.  If it wraps, it may overflow.  */

  if (scev_probably_wraps_p (init, step, stmt, get_chrec_loop (chrec),
			     true))
    return true;

  if (dump_file && (dump_flags & TDF_DETAILS) != 0)
    {
      print_generic_expr (dump_file, var, 0);
      fprintf (dump_file, ": loop information indicates does not overflow\n");
    }

  return false;
}


/* Given two numeric value ranges VR0, VR1 and a comparison code COMP:

   - Return BOOLEAN_TRUE_NODE if VR0 COMP VR1 always returns true for
     all the values in the ranges.

   - Return BOOLEAN_FALSE_NODE if the comparison always returns false.

   - Return NULL_TREE if it is not always possible to determine the
     value of the comparison.

   Also set *STRICT_OVERFLOW_P to indicate whether a range with an
   overflow infinity was used in the test.  */


static tree
compare_ranges (enum tree_code comp, value_range_t *vr0, value_range_t *vr1,
		bool *strict_overflow_p)
{
  /* VARYING or UNDEFINED ranges cannot be compared.  */
  if (vr0->type == VR_VARYING
      || vr0->type == VR_UNDEFINED
      || vr1->type == VR_VARYING
      || vr1->type == VR_UNDEFINED)
    return NULL_TREE;

  /* Anti-ranges need to be handled separately.  */
  if (vr0->type == VR_ANTI_RANGE || vr1->type == VR_ANTI_RANGE)
    {
      /* If both are anti-ranges, then we cannot compute any
	 comparison.  */
      if (vr0->type == VR_ANTI_RANGE && vr1->type == VR_ANTI_RANGE)
	return NULL_TREE;

      /* These comparisons are never statically computable.  */
      if (comp == GT_EXPR
	  || comp == GE_EXPR
	  || comp == LT_EXPR
	  || comp == LE_EXPR)
	return NULL_TREE;

      /* Equality can be computed only between a range and an
	 anti-range.  ~[VAL1, VAL2] == [VAL1, VAL2] is always false.  */
      if (vr0->type == VR_RANGE)
	{
	  /* To simplify processing, make VR0 the anti-range.  */
	  value_range_t *tmp = vr0;
	  vr0 = vr1;
	  vr1 = tmp;
	}

      gcc_assert (comp == NE_EXPR || comp == EQ_EXPR);

      if (compare_values_warnv (vr0->min, vr1->min, strict_overflow_p) == 0
	  && compare_values_warnv (vr0->max, vr1->max, strict_overflow_p) == 0)
	return (comp == NE_EXPR) ? boolean_true_node : boolean_false_node;

      return NULL_TREE;
    }

  if (!usable_range_p (vr0, strict_overflow_p)
      || !usable_range_p (vr1, strict_overflow_p))
    return NULL_TREE;

  /* Simplify processing.  If COMP is GT_EXPR or GE_EXPR, switch the
     operands around and change the comparison code.  */
  if (comp == GT_EXPR || comp == GE_EXPR)
    {
      value_range_t *tmp;
      comp = (comp == GT_EXPR) ? LT_EXPR : LE_EXPR;
      tmp = vr0;
      vr0 = vr1;
      vr1 = tmp;
    }

  if (comp == EQ_EXPR)
    {
      /* Equality may only be computed if both ranges represent
	 exactly one value.  */
      if (compare_values_warnv (vr0->min, vr0->max, strict_overflow_p) == 0
	  && compare_values_warnv (vr1->min, vr1->max, strict_overflow_p) == 0)
	{
	  int cmp_min = compare_values_warnv (vr0->min, vr1->min,
					      strict_overflow_p);
	  int cmp_max = compare_values_warnv (vr0->max, vr1->max,
					      strict_overflow_p);
	  if (cmp_min == 0 && cmp_max == 0)
	    return boolean_true_node;
	  else if (cmp_min != -2 && cmp_max != -2)
	    return boolean_false_node;
	}
      /* If [V0_MIN, V1_MAX] < [V1_MIN, V1_MAX] then V0 != V1.  */
      else if (compare_values_warnv (vr0->min, vr1->max,
				     strict_overflow_p) == 1
	       || compare_values_warnv (vr1->min, vr0->max,
					strict_overflow_p) == 1)
	return boolean_false_node;

      return NULL_TREE;
    }
  else if (comp == NE_EXPR)
    {
      int cmp1, cmp2;

      /* If VR0 is completely to the left or completely to the right
	 of VR1, they are always different.  Notice that we need to
	 make sure that both comparisons yield similar results to
	 avoid comparing values that cannot be compared at
	 compile-time.  */
      cmp1 = compare_values_warnv (vr0->max, vr1->min, strict_overflow_p);
      cmp2 = compare_values_warnv (vr0->min, vr1->max, strict_overflow_p);
      if ((cmp1 == -1 && cmp2 == -1) || (cmp1 == 1 && cmp2 == 1))
	return boolean_true_node;

      /* If VR0 and VR1 represent a single value and are identical,
	 return false.  */
      else if (compare_values_warnv (vr0->min, vr0->max,
				     strict_overflow_p) == 0
	       && compare_values_warnv (vr1->min, vr1->max,
					strict_overflow_p) == 0
	       && compare_values_warnv (vr0->min, vr1->min,
					strict_overflow_p) == 0
	       && compare_values_warnv (vr0->max, vr1->max,
					strict_overflow_p) == 0)
	return boolean_false_node;

      /* Otherwise, they may or may not be different.  */
      else
	return NULL_TREE;
    }
  else if (comp == LT_EXPR || comp == LE_EXPR)
    {
      int tst;

      /* If VR0 is to the left of VR1, return true.  */
      tst = compare_values_warnv (vr0->max, vr1->min, strict_overflow_p);
      if ((comp == LT_EXPR && tst == -1)
	  || (comp == LE_EXPR && (tst == -1 || tst == 0)))
	{
	  if (overflow_infinity_range_p (vr0)
	      || overflow_infinity_range_p (vr1))
	    *strict_overflow_p = true;
	  return boolean_true_node;
	}

      /* If VR0 is to the right of VR1, return false.  */
      tst = compare_values_warnv (vr0->min, vr1->max, strict_overflow_p);
      if ((comp == LT_EXPR && (tst == 0 || tst == 1))
	  || (comp == LE_EXPR && tst == 1))
	{
	  if (overflow_infinity_range_p (vr0)
	      || overflow_infinity_range_p (vr1))
	    *strict_overflow_p = true;
	  return boolean_false_node;
	}

      /* Otherwise, we don't know.  */
      return NULL_TREE;
    }

  gcc_unreachable ();
}


/* Given a value range VR, a value VAL and a comparison code COMP, return
   BOOLEAN_TRUE_NODE if VR COMP VAL always returns true for all the
   values in VR.  Return BOOLEAN_FALSE_NODE if the comparison
   always returns false.  Return NULL_TREE if it is not always
   possible to determine the value of the comparison.  Also set
   *STRICT_OVERFLOW_P to indicate whether a range with an overflow
   infinity was used in the test.  */

static tree
compare_range_with_value (enum tree_code comp, value_range_t *vr, tree val,
			  bool *strict_overflow_p)
{
  if (vr->type == VR_VARYING || vr->type == VR_UNDEFINED)
    return NULL_TREE;

  /* Anti-ranges need to be handled separately.  */
  if (vr->type == VR_ANTI_RANGE)
    {
      /* For anti-ranges, the only predicates that we can compute at
	 compile time are equality and inequality.  */
      if (comp == GT_EXPR
	  || comp == GE_EXPR
	  || comp == LT_EXPR
	  || comp == LE_EXPR)
	return NULL_TREE;

      /* ~[VAL_1, VAL_2] OP VAL is known if VAL_1 <= VAL <= VAL_2.  */
      if (value_inside_range (val, vr) == 1)
	return (comp == NE_EXPR) ? boolean_true_node : boolean_false_node;

      return NULL_TREE;
    }

  if (!usable_range_p (vr, strict_overflow_p))
    return NULL_TREE;

  if (comp == EQ_EXPR)
    {
      /* EQ_EXPR may only be computed if VR represents exactly
	 one value.  */
      if (compare_values_warnv (vr->min, vr->max, strict_overflow_p) == 0)
	{
	  int cmp = compare_values_warnv (vr->min, val, strict_overflow_p);
	  if (cmp == 0)
	    return boolean_true_node;
	  else if (cmp == -1 || cmp == 1 || cmp == 2)
	    return boolean_false_node;
	}
      else if (compare_values_warnv (val, vr->min, strict_overflow_p) == -1
	       || compare_values_warnv (vr->max, val, strict_overflow_p) == -1)
	return boolean_false_node;

      return NULL_TREE;
    }
  else if (comp == NE_EXPR)
    {
      /* If VAL is not inside VR, then they are always different.  */
      if (compare_values_warnv (vr->max, val, strict_overflow_p) == -1
	  || compare_values_warnv (vr->min, val, strict_overflow_p) == 1)
	return boolean_true_node;

      /* If VR represents exactly one value equal to VAL, then return
	 false.  */
      if (compare_values_warnv (vr->min, vr->max, strict_overflow_p) == 0
	  && compare_values_warnv (vr->min, val, strict_overflow_p) == 0)
	return boolean_false_node;

      /* Otherwise, they may or may not be different.  */
      return NULL_TREE;
    }
  else if (comp == LT_EXPR || comp == LE_EXPR)
    {
      int tst;

      /* If VR is to the left of VAL, return true.  */
      tst = compare_values_warnv (vr->max, val, strict_overflow_p);
      if ((comp == LT_EXPR && tst == -1)
	  || (comp == LE_EXPR && (tst == -1 || tst == 0)))
	{
	  if (overflow_infinity_range_p (vr))
	    *strict_overflow_p = true;
	  return boolean_true_node;
	}

      /* If VR is to the right of VAL, return false.  */
      tst = compare_values_warnv (vr->min, val, strict_overflow_p);
      if ((comp == LT_EXPR && (tst == 0 || tst == 1))
	  || (comp == LE_EXPR && tst == 1))
	{
	  if (overflow_infinity_range_p (vr))
	    *strict_overflow_p = true;
	  return boolean_false_node;
	}

      /* Otherwise, we don't know.  */
      return NULL_TREE;
    }
  else if (comp == GT_EXPR || comp == GE_EXPR)
    {
      int tst;

      /* If VR is to the right of VAL, return true.  */
      tst = compare_values_warnv (vr->min, val, strict_overflow_p);
      if ((comp == GT_EXPR && tst == 1)
	  || (comp == GE_EXPR && (tst == 0 || tst == 1)))
	{
	  if (overflow_infinity_range_p (vr))
	    *strict_overflow_p = true;
	  return boolean_true_node;
	}

      /* If VR is to the left of VAL, return false.  */
      tst = compare_values_warnv (vr->max, val, strict_overflow_p);
      if ((comp == GT_EXPR && (tst == -1 || tst == 0))
	  || (comp == GE_EXPR && tst == -1))
	{
	  if (overflow_infinity_range_p (vr))
	    *strict_overflow_p = true;
	  return boolean_false_node;
	}

      /* Otherwise, we don't know.  */
      return NULL_TREE;
    }

  gcc_unreachable ();
}


/* Debugging dumps.  */

static void dump_value_range (FILE *, value_range_t *);
static void debug_value_range (value_range_t *);
static void dump_all_value_ranges (FILE *);
static void debug_all_value_ranges (void);
static void dump_vr_equiv (FILE *, bitmap);
static void debug_vr_equiv (bitmap);


/* Dump value range VR to FILE.  */

static void
dump_value_range (FILE *file, value_range_t *vr)
{
  if (vr == NULL)
    fprintf (file, "[]");
  else if (vr->type == VR_UNDEFINED)
    fprintf (file, "UNDEFINED");
  else if (vr->type == VR_RANGE || vr->type == VR_ANTI_RANGE)
    {
      tree type = TREE_TYPE (vr->min);

      fprintf (file, "%s[", (vr->type == VR_ANTI_RANGE) ? "~" : "");

      if (is_negative_overflow_infinity (vr->min))
	fprintf (file, "-INF(OVF)");
      else if (INTEGRAL_TYPE_P (type)
	       && !TYPE_UNSIGNED (type)
	       && vrp_val_is_min (vr->min))
	fprintf (file, "-INF");
      else
	print_generic_expr (file, vr->min, 0);

      fprintf (file, ", ");

      if (is_positive_overflow_infinity (vr->max))
	fprintf (file, "+INF(OVF)");
      else if (INTEGRAL_TYPE_P (type)
	       && vrp_val_is_max (vr->max))
	fprintf (file, "+INF");
      else
	print_generic_expr (file, vr->max, 0);

      fprintf (file, "]");

      if (vr->equiv)
	{
	  bitmap_iterator bi;
	  unsigned i, c = 0;

	  fprintf (file, "  EQUIVALENCES: { ");

	  EXECUTE_IF_SET_IN_BITMAP (vr->equiv, 0, i, bi)
	    {
	      print_generic_expr (file, ssa_name (i), 0);
	      fprintf (file, " ");
	      c++;
	    }

	  fprintf (file, "} (%u elements)", c);
	}
    }
  else if (vr->type == VR_VARYING)
    fprintf (file, "VARYING");
  else
    fprintf (file, "INVALID RANGE");
}


/* Dump value range VR to stderr.  */

static void
debug_value_range (value_range_t *vr)
{
  dump_value_range (stderr, vr);
  fprintf (stderr, "\n");
}


/* Dump value ranges of all SSA_NAMEs to FILE.  */

static void
dump_all_value_ranges (FILE *file)
{
  size_t i;

  for (i = 0; i < num_vr_values; i++)
    {
      if (vr_value[i])
	{
	  print_generic_expr (file, ssa_name (i), 0);
	  fprintf (file, ": ");
	  dump_value_range (file, vr_value[i]);
	  fprintf (file, "\n");
	}
    }

  fprintf (file, "\n");
}


/* Dump all value ranges to stderr.  */

static void
debug_all_value_ranges (void)
{
  dump_all_value_ranges (stderr);
}


/* Given a COND_EXPR COND of the form 'V OP W', and an SSA name V,
   create a new SSA name N and return the assertion assignment
   'V = ASSERT_EXPR <V, V OP W>'.  */

static GIMPLE_type
build_assert_expr_for (tree cond, tree v)
{
  tree n;
  GIMPLE_type assertion;

  gcc_assert (TREE_CODE (v) == SSA_NAME);
  n = duplicate_ssa_name (v, NULL);

  if (COMPARISON_CLASS_P (cond))
    {
      tree a = build2 (ASSERT_EXPR, TREE_TYPE (v), v, cond);
      assertion = gimple_build_assign (n, a);
    }
  else if (TREE_CODE (cond) == SSA_NAME)
    {
      /* Given V, build the assignment N = true.  */
      gcc_assert (v == cond);
      assertion = gimple_build_assign (n, boolean_true_node);
    }
  else
    gcc_unreachable ();

  SSA_NAME_DEF_STMT (n) = assertion;

  /* The new ASSERT_EXPR, creates a new SSA name that replaces the
     operand of the ASSERT_EXPR. Register the new name and the old one
     in the replacement table so that we can fix the SSA web after
     adding all the ASSERT_EXPRs.  */
  register_new_name_mapping (n, v);

  return assertion;
}


/* Return false if EXPR is a predicate expression involving floating
   point values.  */

static inline bool
fp_predicate (GIMPLE_type stmt)
{
  GIMPLE_CHECK (stmt, GIMPLE_COND);

  return FLOAT_TYPE_P (TREE_TYPE (gimple_cond_lhs (stmt)));
}


/* If the range of values taken by OP can be inferred after STMT executes,
   return the comparison code (COMP_CODE_P) and value (VAL_P) that
   describes the inferred range.  Return true if a range could be
   inferred.  */

static bool
infer_value_range (GIMPLE_type stmt, tree op, enum tree_code *comp_code_p, tree *val_p)
{
  *val_p = NULL_TREE;
  *comp_code_p = ERROR_MARK;

  /* Do not attempt to infer anything in names that flow through
     abnormal edges.  */
  if (SSA_NAME_OCCURS_IN_ABNORMAL_PHI (op))
    return false;

  /* Similarly, don't infer anything from statements that may throw
     exceptions.  */
  if (stmt_could_throw_p (stmt))
    return false;

  /* If STMT is the last statement of a basic block with no
     successors, there is no point inferring anything about any of its
     operands.  We would not be able to find a proper insertion point
     for the assertion, anyway.  */
  if (stmt_ends_bb_p (stmt) && EDGE_COUNT (gimple_bb (stmt)->succs) == 0)
    return false;

  /* We can only assume that a pointer dereference will yield
     non-NULL if -fdelete-null-pointer-checks is enabled.  */
  if (flag_delete_null_pointer_checks
      && POINTER_TYPE_P (TREE_TYPE (op))
      && gimple_code (stmt) != GIMPLE_ASM)
    {
      unsigned num_uses, num_loads, num_stores;

      count_uses_and_derefs (op, stmt, &num_uses, &num_loads, &num_stores);
      if (num_loads + num_stores > 0)
	{
	  *val_p = build_int_cst (TREE_TYPE (op), 0);
	  *comp_code_p = NE_EXPR;
	  return true;
	}
    }

  return false;
}


static void dump_asserts_for (FILE *, tree);
static void debug_asserts_for (tree);
static void dump_all_asserts (FILE *);
static void debug_all_asserts (void);

/* Dump all the registered assertions for NAME to FILE.  */

static void
dump_asserts_for (FILE *file, tree name)
{
  assert_locus_t loc;

  fprintf (file, "Assertions to be inserted for ");
  print_generic_expr (file, name, 0);
  fprintf (file, "\n");

  loc = asserts_for[SSA_NAME_VERSION (name)];
  while (loc)
    {
      fprintf (file, "\t");
      print_gimple_stmt (file, gsi_stmt (loc->si), 0, 0);
      fprintf (file, "\n\tBB #%d", loc->bb->index);
      if (loc->e)
	{
	  fprintf (file, "\n\tEDGE %d->%d", loc->e->src->index,
	           loc->e->dest->index);
	  dump_edge_info (file, loc->e, 0);
	}
      fprintf (file, "\n\tPREDICATE: ");
      print_generic_expr (file, name, 0);
      fprintf (file, " %s ", GET_TREE_CODE_NAME(loc->comp_code));
      print_generic_expr (file, loc->val, 0);
      fprintf (file, "\n\n");
      loc = loc->next;
    }

  fprintf (file, "\n");
}


/* Dump all the registered assertions for NAME to stderr.  */

static void
debug_asserts_for (tree name)
{
  dump_asserts_for (stderr, name);
}


/* Dump all the registered assertions for all the names to FILE.  */

static void
dump_all_asserts (FILE *file)
{
  unsigned i;
  bitmap_iterator bi;

  fprintf (file, "\nASSERT_EXPRs to be inserted\n\n");
  EXECUTE_IF_SET_IN_BITMAP (need_assert_for, 0, i, bi)
    dump_asserts_for (file, ssa_name (i));
  fprintf (file, "\n");
}


/* Dump all the registered assertions for all the names to stderr.  */

static void
debug_all_asserts (void)
{
  dump_all_asserts (stderr);
}


/* If NAME doesn't have an ASSERT_EXPR registered for asserting
   'EXPR COMP_CODE VAL' at a location that dominates block BB or
   E->DEST, then register this location as a possible insertion point
   for ASSERT_EXPR <NAME, EXPR COMP_CODE VAL>.

   BB, E and SI provide the exact insertion point for the new
   ASSERT_EXPR.  If BB is NULL, then the ASSERT_EXPR is to be inserted
   on edge E.  Otherwise, if E is NULL, the ASSERT_EXPR is inserted on
   BB.  If SI points to a COND_EXPR or a SWITCH_EXPR statement, then E
   must not be NULL.  */

static void
register_new_assert_for (tree name, tree expr,
			 enum tree_code comp_code,
			 tree val,
			 basic_block bb,
			 edge e,
			 gimple_stmt_iterator si)
{
  assert_locus_t n, loc, last_loc;
  basic_block dest_bb;

  gcc_checking_assert (bb == NULL || e == NULL);

  if (e == NULL)
    gcc_checking_assert (gimple_code (gsi_stmt (si)) != GIMPLE_COND
			 && gimple_code (gsi_stmt (si)) != GIMPLE_SWITCH);

  /* Never build an assert comparing against an integer constant with
     TREE_OVERFLOW set.  This confuses our undefined overflow warning
     machinery.  */
  if (TREE_CODE (val) == INTEGER_CST
      && TREE_OVERFLOW (val))
    val = build_int_cst_wide (TREE_TYPE (val),
			      TREE_INT_CST_LOW (val), TREE_INT_CST_HIGH (val));

  /* The new assertion A will be inserted at BB or E.  We need to
     determine if the new location is dominated by a previously
     registered location for A.  If we are doing an edge insertion,
     assume that A will be inserted at E->DEST.  Note that this is not
     necessarily true.

     If E is a critical edge, it will be split.  But even if E is
     split, the new block will dominate the same set of blocks that
     E->DEST dominates.

     The reverse, however, is not true, blocks dominated by E->DEST
     will not be dominated by the new block created to split E.  So,
     if the insertion location is on a critical edge, we will not use
     the new location to move another assertion previously registered
     at a block dominated by E->DEST.  */
  dest_bb = (bb) ? bb : e->dest;

  /* If NAME already has an ASSERT_EXPR registered for COMP_CODE and
     VAL at a block dominating DEST_BB, then we don't need to insert a new
     one.  Similarly, if the same assertion already exists at a block
     dominated by DEST_BB and the new location is not on a critical
     edge, then update the existing location for the assertion (i.e.,
     move the assertion up in the dominance tree).

     Note, this is implemented as a simple linked list because there
     should not be more than a handful of assertions registered per
     name.  If this becomes a performance problem, a table hashed by
     COMP_CODE and VAL could be implemented.  */
  loc = asserts_for[SSA_NAME_VERSION (name)];
  last_loc = loc;
  while (loc)
    {
      if (loc->comp_code == comp_code
	  && (loc->val == val
	      || operand_equal_p (loc->val, val, 0))
	  && (loc->expr == expr
	      || operand_equal_p (loc->expr, expr, 0)))
	{
	  /* If the assertion NAME COMP_CODE VAL has already been
	     registered at a basic block that dominates DEST_BB, then
	     we don't need to insert the same assertion again.  Note
	     that we don't check strict dominance here to avoid
	     replicating the same assertion inside the same basic
	     block more than once (e.g., when a pointer is
	     dereferenced several times inside a block).

	     An exception to this rule are edge insertions.  If the
	     new assertion is to be inserted on edge E, then it will
	     dominate all the other insertions that we may want to
	     insert in DEST_BB.  So, if we are doing an edge
	     insertion, don't do this dominance check.  */
          if (e == NULL
	      && dominated_by_p (CDI_DOMINATORS, dest_bb, loc->bb))
	    return;

	  /* Otherwise, if E is not a critical edge and DEST_BB
	     dominates the existing location for the assertion, move
	     the assertion up in the dominance tree by updating its
	     location information.  */
	  if ((e == NULL || !EDGE_CRITICAL_P (e))
	      && dominated_by_p (CDI_DOMINATORS, loc->bb, dest_bb))
	    {
	      loc->bb = dest_bb;
	      loc->e = e;
	      loc->si = si;
	      return;
	    }
	}

      /* Update the last node of the list and move to the next one.  */
      last_loc = loc;
      loc = loc->next;
    }

  /* If we didn't find an assertion already registered for
     NAME COMP_CODE VAL, add a new one at the end of the list of
     assertions associated with NAME.  */
  n = XNEW (struct assert_locus_d);
  n->bb = dest_bb;
  n->e = e;
  n->si = si;
  n->comp_code = comp_code;
  n->val = val;
  n->expr = expr;
  n->next = NULL;

  if (last_loc)
    last_loc->next = n;
  else
    asserts_for[SSA_NAME_VERSION (name)] = n;

  bitmap_set_bit (need_assert_for, SSA_NAME_VERSION (name));
}

/* (COND_OP0 COND_CODE COND_OP1) is a predicate which uses NAME.
   Extract a suitable test code and value and store them into *CODE_P and
   *VAL_P so the predicate is normalized to NAME *CODE_P *VAL_P.

   If no extraction was possible, return FALSE, otherwise return TRUE.

   If INVERT is true, then we invert the result stored into *CODE_P.  */

static bool
extract_code_and_val_from_cond_with_ops (tree name, enum tree_code cond_code,
					 tree cond_op0, tree cond_op1,
					 bool invert, enum tree_code *code_p,
					 tree *val_p)
{
  enum tree_code comp_code;
  tree val;

  /* Otherwise, we have a comparison of the form NAME COMP VAL
     or VAL COMP NAME.  */
  if (name == cond_op1)
    {
      /* If the predicate is of the form VAL COMP NAME, flip
	 COMP around because we need to register NAME as the
	 first operand in the predicate.  */
      comp_code = swap_tree_comparison (cond_code);
      val = cond_op0;
    }
  else
    {
      /* The comparison is of the form NAME COMP VAL, so the
	 comparison code remains unchanged.  */
      comp_code = cond_code;
      val = cond_op1;
    }

  /* Invert the comparison code as necessary.  */
  if (invert)
    comp_code = invert_tree_comparison (comp_code, 0);

  /* VRP does not handle float types.  */
  if (SCALAR_FLOAT_TYPE_P (TREE_TYPE (val)))
    return false;

  /* Do not register always-false predicates.
     FIXME:  this works around a limitation in fold() when dealing with
     enumerations.  Given 'enum { N1, N2 } x;', fold will not
     fold 'if (x > N2)' to 'if (0)'.  */
  if ((comp_code == GT_EXPR || comp_code == LT_EXPR)
      && INTEGRAL_TYPE_P (TREE_TYPE (val)))
    {
      tree min = TYPE_MIN_VALUE (TREE_TYPE (val));
      tree max = TYPE_MAX_VALUE (TREE_TYPE (val));

      if (comp_code == GT_EXPR
	  && (!max
	      || compare_values (val, max) == 0))
	return false;

      if (comp_code == LT_EXPR
	  && (!min
	      || compare_values (val, min) == 0))
	return false;
    }
  *code_p = comp_code;
  *val_p = val;
  return true;
}

/* Try to register an edge assertion for SSA name NAME on edge E for
   the condition COND contributing to the conditional jump pointed to by BSI.
   Invert the condition COND if INVERT is true.
   Return true if an assertion for NAME could be registered.  */

static bool
register_edge_assert_for_2 (tree name, edge e, gimple_stmt_iterator bsi,
			    enum tree_code cond_code,
			    tree cond_op0, tree cond_op1, bool invert)
{
  tree val;
  enum tree_code comp_code;
  bool retval = false;

  if (!extract_code_and_val_from_cond_with_ops (name, cond_code,
						cond_op0,
						cond_op1,
						invert, &comp_code, &val))
    return false;

  /* Only register an ASSERT_EXPR if NAME was found in the sub-graph
     reachable from E.  */
  if (live_on_edge (e, name)
      && !has_single_use (name))
    {
      register_new_assert_for (name, name, comp_code, val, NULL, e, bsi);
      retval = true;
    }

  /* In the case of NAME <= CST and NAME being defined as
     NAME = (unsigned) NAME2 + CST2 we can assert NAME2 >= -CST2
     and NAME2 <= CST - CST2.  We can do the same for NAME > CST.
     This catches range and anti-range tests.  */
  if ((comp_code == LE_EXPR
       || comp_code == GT_EXPR)
      && TREE_CODE (val) == INTEGER_CST
      && TYPE_UNSIGNED (TREE_TYPE (val)))
    {
      GIMPLE_type def_stmt = SSA_NAME_DEF_STMT (name);
      tree cst2 = NULL_TREE, name2 = NULL_TREE, name3 = NULL_TREE;

      /* Extract CST2 from the (optional) addition.  */
      if (is_gimple_assign (def_stmt)
	  && gimple_assign_rhs_code (def_stmt) == PLUS_EXPR)
	{
	  name2 = gimple_assign_rhs1 (def_stmt);
	  cst2 = gimple_assign_rhs2 (def_stmt);
	  if (TREE_CODE (name2) == SSA_NAME
	      && TREE_CODE (cst2) == INTEGER_CST)
	    def_stmt = SSA_NAME_DEF_STMT (name2);
	}

      /* Extract NAME2 from the (optional) sign-changing cast.  */
      if (gimple_assign_cast_p (def_stmt))
	{
	  if (CONVERT_EXPR_CODE_P (gimple_assign_rhs_code (def_stmt))
	      && ! TYPE_UNSIGNED (TREE_TYPE (gimple_assign_rhs1 (def_stmt)))
	      && (TYPE_PRECISION (gimple_expr_type (def_stmt))
		  == TYPE_PRECISION (TREE_TYPE (gimple_assign_rhs1 (def_stmt)))))
	    name3 = gimple_assign_rhs1 (def_stmt);
	}

      /* If name3 is used later, create an ASSERT_EXPR for it.  */
      if (name3 != NULL_TREE
      	  && TREE_CODE (name3) == SSA_NAME
	  && (cst2 == NULL_TREE
	      || TREE_CODE (cst2) == INTEGER_CST)
	  && INTEGRAL_TYPE_P (TREE_TYPE (name3))
	  && live_on_edge (e, name3)
	  && !has_single_use (name3))
	{
	  tree tmp;

	  /* Build an expression for the range test.  */
	  tmp = build1 (NOP_EXPR, TREE_TYPE (name), name3);
	  if (cst2 != NULL_TREE)
	    tmp = build2 (PLUS_EXPR, TREE_TYPE (name), tmp, cst2);

	  if (dump_file)
	    {
	      fprintf (dump_file, "Adding assert for ");
	      print_generic_expr (dump_file, name3, 0);
	      fprintf (dump_file, " from ");
	      print_generic_expr (dump_file, tmp, 0);
	      fprintf (dump_file, "\n");
	    }

	  register_new_assert_for (name3, tmp, comp_code, val, NULL, e, bsi);

	  retval = true;
	}

      /* If name2 is used later, create an ASSERT_EXPR for it.  */
      if (name2 != NULL_TREE
      	  && TREE_CODE (name2) == SSA_NAME
	  && TREE_CODE (cst2) == INTEGER_CST
	  && INTEGRAL_TYPE_P (TREE_TYPE (name2))
	  && live_on_edge (e, name2)
	  && !has_single_use (name2))
	{
	  tree tmp;

	  /* Build an expression for the range test.  */
	  tmp = name2;
	  if (TREE_TYPE (name) != TREE_TYPE (name2))
	    tmp = build1 (NOP_EXPR, TREE_TYPE (name), tmp);
	  if (cst2 != NULL_TREE)
	    tmp = build2 (PLUS_EXPR, TREE_TYPE (name), tmp, cst2);

	  if (dump_file)
	    {
	      fprintf (dump_file, "Adding assert for ");
	      print_generic_expr (dump_file, name2, 0);
	      fprintf (dump_file, " from ");
	      print_generic_expr (dump_file, tmp, 0);
	      fprintf (dump_file, "\n");
	    }

	  register_new_assert_for (name2, tmp, comp_code, val, NULL, e, bsi);

	  retval = true;
	}
    }

  return retval;
}

/* OP is an operand of a truth value expression which is known to have
   a particular value.  Register any asserts for OP and for any
   operands in OP's defining statement.

   If CODE is EQ_EXPR, then we want to register OP is zero (false),
   if CODE is NE_EXPR, then we want to register OP is nonzero (true).   */

static bool
register_edge_assert_for_1 (tree op, enum tree_code code,
			    edge e, gimple_stmt_iterator bsi)
{
  bool retval = false;
  GIMPLE_type op_def;
  tree val;
  enum tree_code rhs_code;

  /* We only care about SSA_NAMEs.  */
  if (TREE_CODE (op) != SSA_NAME)
    return false;

  /* We know that OP will have a zero or nonzero value.  If OP is used
     more than once go ahead and register an assert for OP.

     The FOUND_IN_SUBGRAPH support is not helpful in this situation as
     it will always be set for OP (because OP is used in a COND_EXPR in
     the subgraph).  */
  if (!has_single_use (op))
    {
      val = build_int_cst (TREE_TYPE (op), 0);
      register_new_assert_for (op, op, code, val, NULL, e, bsi);
      retval = true;
    }

  /* Now look at how OP is set.  If it's set from a comparison,
     a truth operation or some bit operations, then we may be able
     to register information about the operands of that assignment.  */
  op_def = SSA_NAME_DEF_STMT (op);
  if (gimple_code (op_def) != GIMPLE_ASSIGN)
    return retval;

  rhs_code = gimple_assign_rhs_code (op_def);

  if (TREE_CODE_CLASS (rhs_code) == tcc_comparison)
    {
      bool invert = (code == EQ_EXPR ? true : false);
      tree op0 = gimple_assign_rhs1 (op_def);
      tree op1 = gimple_assign_rhs2 (op_def);

      if (TREE_CODE (op0) == SSA_NAME)
        retval |= register_edge_assert_for_2 (op0, e, bsi, rhs_code, op0, op1,
					      invert);
      if (TREE_CODE (op1) == SSA_NAME)
        retval |= register_edge_assert_for_2 (op1, e, bsi, rhs_code, op0, op1,
					      invert);
    }
  else if ((code == NE_EXPR
	    && gimple_assign_rhs_code (op_def) == BIT_AND_EXPR)
	   || (code == EQ_EXPR
	       && gimple_assign_rhs_code (op_def) == BIT_IOR_EXPR))
    {
      /* Recurse on each operand.  */
      retval |= register_edge_assert_for_1 (gimple_assign_rhs1 (op_def),
					    code, e, bsi);
      retval |= register_edge_assert_for_1 (gimple_assign_rhs2 (op_def),
					    code, e, bsi);
    }
  else if (gimple_assign_rhs_code (op_def) == BIT_NOT_EXPR
	   && TYPE_PRECISION (TREE_TYPE (gimple_assign_lhs (op_def))) == 1)
    {
      /* Recurse, flipping CODE.  */
      code = invert_tree_comparison (code, false);
      retval |= register_edge_assert_for_1 (gimple_assign_rhs1 (op_def),
					    code, e, bsi);
    }
  else if (gimple_assign_rhs_code (op_def) == SSA_NAME)
    {
      /* Recurse through the copy.  */
      retval |= register_edge_assert_for_1 (gimple_assign_rhs1 (op_def),
					    code, e, bsi);
    }
  else if (CONVERT_EXPR_CODE_P (gimple_assign_rhs_code (op_def)))
    {
      /* Recurse through the type conversion.  */
      retval |= register_edge_assert_for_1 (gimple_assign_rhs1 (op_def),
					    code, e, bsi);
    }

  return retval;
}

/* Try to register an edge assertion for SSA name NAME on edge E for
   the condition COND contributing to the conditional jump pointed to by SI.
   Return true if an assertion for NAME could be registered.  */

static bool
register_edge_assert_for (tree name, edge e, gimple_stmt_iterator si,
			  enum tree_code cond_code, tree cond_op0,
			  tree cond_op1)
{
  tree val;
  enum tree_code comp_code;
  bool retval = false;
  bool is_else_edge = (e->flags & EDGE_FALSE_VALUE) != 0;

  /* Do not attempt to infer anything in names that flow through
     abnormal edges.  */
  if (SSA_NAME_OCCURS_IN_ABNORMAL_PHI (name))
    return false;

  if (!extract_code_and_val_from_cond_with_ops (name, cond_code,
						cond_op0, cond_op1,
						is_else_edge,
						&comp_code, &val))
    return false;

  /* Register ASSERT_EXPRs for name.  */
  retval |= register_edge_assert_for_2 (name, e, si, cond_code, cond_op0,
					cond_op1, is_else_edge);


  /* If COND is effectively an equality test of an SSA_NAME against
     the value zero or one, then we may be able to assert values
     for SSA_NAMEs which flow into COND.  */

  /* In the case of NAME == 1 or NAME != 0, for BIT_AND_EXPR defining
     statement of NAME we can assert both operands of the BIT_AND_EXPR
     have nonzero value.  */
  if (((comp_code == EQ_EXPR && integer_onep (val))
       || (comp_code == NE_EXPR && integer_zerop (val))))
    {
      GIMPLE_type def_stmt = SSA_NAME_DEF_STMT (name);

      if (is_gimple_assign (def_stmt)
	  && gimple_assign_rhs_code (def_stmt) == BIT_AND_EXPR)
	{
	  tree op0 = gimple_assign_rhs1 (def_stmt);
	  tree op1 = gimple_assign_rhs2 (def_stmt);
	  retval |= register_edge_assert_for_1 (op0, NE_EXPR, e, si);
	  retval |= register_edge_assert_for_1 (op1, NE_EXPR, e, si);
	}
    }

  /* In the case of NAME == 0 or NAME != 1, for BIT_IOR_EXPR defining
     statement of NAME we can assert both operands of the BIT_IOR_EXPR
     have zero value.  */
  if (((comp_code == EQ_EXPR && integer_zerop (val))
       || (comp_code == NE_EXPR && integer_onep (val))))
    {
      GIMPLE_type def_stmt = SSA_NAME_DEF_STMT (name);

      /* For BIT_IOR_EXPR only if NAME == 0 both operands have
	 necessarily zero value, or if type-precision is one.  */
      if (is_gimple_assign (def_stmt)
	  && (gimple_assign_rhs_code (def_stmt) == BIT_IOR_EXPR
	      && (TYPE_PRECISION (TREE_TYPE (name)) == 1
	          || comp_code == EQ_EXPR)))
	{
	  tree op0 = gimple_assign_rhs1 (def_stmt);
	  tree op1 = gimple_assign_rhs2 (def_stmt);
	  retval |= register_edge_assert_for_1 (op0, EQ_EXPR, e, si);
	  retval |= register_edge_assert_for_1 (op1, EQ_EXPR, e, si);
	}
    }

  return retval;
}


/* Determine whether the outgoing edges of BB should receive an
   ASSERT_EXPR for each of the operands of BB's LAST statement.
   The last statement of BB must be a COND_EXPR.

   If any of the sub-graphs rooted at BB have an interesting use of
   the predicate operands, an assert location node is added to the
   list of assertions for the corresponding operands.  */

static bool
find_conditional_asserts (basic_block bb, GIMPLE_type last)
{
  bool need_assert;
  gimple_stmt_iterator bsi;
  tree op;
  edge_iterator ei;
  edge e;
  ssa_op_iter iter;

  need_assert = false;
  bsi = gsi_for_stmt (last);

  /* Look for uses of the operands in each of the sub-graphs
     rooted at BB.  We need to check each of the outgoing edges
     separately, so that we know what kind of ASSERT_EXPR to
     insert.  */
  FOR_EACH_EDGE (e, ei, bb->succs)
    {
      if (e->dest == bb)
	continue;

      /* Register the necessary assertions for each operand in the
	 conditional predicate.  */
      FOR_EACH_SSA_TREE_OPERAND (op, last, iter, SSA_OP_USE)
	{
	  need_assert |= register_edge_assert_for (op, e, bsi,
						   gimple_cond_code (last),
						   gimple_cond_lhs (last),
						   gimple_cond_rhs (last));
	}
    }

  return need_assert;
}

struct case_info
{
  tree expr;
  basic_block bb;
};

/* Compare two case labels sorting first by the destination bb index
   and then by the case value.  */

static int
compare_case_labels (const void *p1, const void *p2)
{
  const struct case_info *ci1 = (const struct case_info *) p1;
  const struct case_info *ci2 = (const struct case_info *) p2;
  int idx1 = ci1->bb->index;
  int idx2 = ci2->bb->index;

  if (idx1 < idx2)
    return -1;
  else if (idx1 == idx2)
    {
      /* Make sure the default label is first in a group.  */
      if (!CASE_LOW (ci1->expr))
	return -1;
      else if (!CASE_LOW (ci2->expr))
	return 1;
      else
	return tree_int_cst_compare (CASE_LOW (ci1->expr),
				     CASE_LOW (ci2->expr));
    }
  else
    return 1;
}

/* Determine whether the outgoing edges of BB should receive an
   ASSERT_EXPR for each of the operands of BB's LAST statement.
   The last statement of BB must be a SWITCH_EXPR.

   If any of the sub-graphs rooted at BB have an interesting use of
   the predicate operands, an assert location node is added to the
   list of assertions for the corresponding operands.  */

static bool
find_switch_asserts (basic_block bb, GIMPLE_type last)
{
  bool need_assert;
  gimple_stmt_iterator bsi;
  tree op;
  edge e;
  struct case_info *ci;
  size_t n = gimple_switch_num_labels (last);
#if GCC_VERSION >= 4000
  unsigned int idx;
#else
  /* Work around GCC 3.4 bug (PR 37086).  */
  volatile unsigned int idx;
#endif

  need_assert = false;
  bsi = gsi_for_stmt (last);
  op = gimple_switch_index (last);
  if (TREE_CODE (op) != SSA_NAME)
    return false;

  /* Build a vector of case labels sorted by destination label.  */
  ci = XNEWVEC (struct case_info, n);
  for (idx = 0; idx < n; ++idx)
    {
      ci[idx].expr = gimple_switch_label (last, idx);
      ci[idx].bb = label_to_block (CASE_LABEL (ci[idx].expr));
    }
  qsort (ci, n, sizeof (struct case_info), compare_case_labels);

  for (idx = 0; idx < n; ++idx)
    {
      tree min, max;
      tree cl = ci[idx].expr;
      basic_block cbb = ci[idx].bb;

      min = CASE_LOW (cl);
      max = CASE_HIGH (cl);

      /* If there are multiple case labels with the same destination
	 we need to combine them to a single value range for the edge.  */
      if (idx + 1 < n && cbb == ci[idx + 1].bb)
	{
	  /* Skip labels until the last of the group.  */
	  do {
	    ++idx;
	  } while (idx < n && cbb == ci[idx].bb);
	  --idx;

	  /* Pick up the maximum of the case label range.  */
	  if (CASE_HIGH (ci[idx].expr))
	    max = CASE_HIGH (ci[idx].expr);
	  else
	    max = CASE_LOW (ci[idx].expr);
	}

      /* Nothing to do if the range includes the default label until we
	 can register anti-ranges.  */
      if (min == NULL_TREE)
	continue;

      /* Find the edge to register the assert expr on.  */
      e = find_edge (bb, cbb);

      /* Register the necessary assertions for the operand in the
	 SWITCH_EXPR.  */
      need_assert |= register_edge_assert_for (op, e, bsi,
					       max ? GE_EXPR : EQ_EXPR,
					       op,
					       fold_convert (TREE_TYPE (op),
							     min));
      if (max)
	{
	  need_assert |= register_edge_assert_for (op, e, bsi, LE_EXPR,
						   op,
						   fold_convert (TREE_TYPE (op),
								 max));
	}
    }

  XDELETEVEC (ci);
  return need_assert;
}


/* Traverse all the statements in block BB looking for statements that
   may generate useful assertions for the SSA names in their operand.
   If a statement produces a useful assertion A for name N_i, then the
   list of assertions already generated for N_i is scanned to
   determine if A is actually needed.

   If N_i already had the assertion A at a location dominating the
   current location, then nothing needs to be done.  Otherwise, the
   new location for A is recorded instead.

   1- For every statement S in BB, all the variables used by S are
      added to bitmap FOUND_IN_SUBGRAPH.

   2- If statement S uses an operand N in a way that exposes a known
      value range for N, then if N was not already generated by an
      ASSERT_EXPR, create a new assert location for N.  For instance,
      if N is a pointer and the statement dereferences it, we can
      assume that N is not NULL.

   3- COND_EXPRs are a special case of #2.  We can derive range
      information from the predicate but need to insert different
      ASSERT_EXPRs for each of the sub-graphs rooted at the
      conditional block.  If the last statement of BB is a conditional
      expression of the form 'X op Y', then

      a) Remove X and Y from the set FOUND_IN_SUBGRAPH.

      b) If the conditional is the only entry point to the sub-graph
	 corresponding to the THEN_CLAUSE, recurse into it.  On
	 return, if X and/or Y are marked in FOUND_IN_SUBGRAPH, then
	 an ASSERT_EXPR is added for the corresponding variable.

      c) Repeat step (b) on the ELSE_CLAUSE.

      d) Mark X and Y in FOUND_IN_SUBGRAPH.

      For instance,

	    if (a == 9)
	      b = a;
	    else
	      b = c + 1;

      In this case, an assertion on the THEN clause is useful to
      determine that 'a' is always 9 on that edge.  However, an assertion
      on the ELSE clause would be unnecessary.

   4- If BB does not end in a conditional expression, then we recurse
      into BB's dominator children.

   At the end of the recursive traversal, every SSA name will have a
   list of locations where ASSERT_EXPRs should be added.  When a new
   location for name N is found, it is registered by calling
   register_new_assert_for.  That function keeps track of all the
   registered assertions to prevent adding unnecessary assertions.
   For instance, if a pointer P_4 is dereferenced more than once in a
   dominator tree, only the location dominating all the dereference of
   P_4 will receive an ASSERT_EXPR.

   If this function returns true, then it means that there are names
   for which we need to generate ASSERT_EXPRs.  Those assertions are
   inserted by process_assert_insertions.  */

static bool
find_assert_locations_1 (basic_block bb, sbitmap live)
{
  gimple_stmt_iterator si;
  GIMPLE_type last;
  GIMPLE_type phi;
  bool need_assert;

  need_assert = false;
  last = last_stmt (bb);

  /* If BB's last statement is a conditional statement involving integer
     operands, determine if we need to add ASSERT_EXPRs.  */
  if (last
      && gimple_code (last) == GIMPLE_COND
      && !fp_predicate (last)
      && !ZERO_SSA_OPERANDS (last, SSA_OP_USE))
    need_assert |= find_conditional_asserts (bb, last);

  /* If BB's last statement is a switch statement involving integer
     operands, determine if we need to add ASSERT_EXPRs.  */
  if (last
      && gimple_code (last) == GIMPLE_SWITCH
      && !ZERO_SSA_OPERANDS (last, SSA_OP_USE))
    need_assert |= find_switch_asserts (bb, last);

  /* Traverse all the statements in BB marking used names and looking
     for statements that may infer assertions for their used operands.  */
  for (si = gsi_start_bb (bb); !gsi_end_p (si); gsi_next (&si))
    {
      GIMPLE_type stmt;
      tree op;
      ssa_op_iter i;

      stmt = gsi_stmt (si);

      if (is_gimple_debug (stmt))
	continue;

      /* See if we can derive an assertion for any of STMT's operands.  */
      FOR_EACH_SSA_TREE_OPERAND (op, stmt, i, SSA_OP_USE)
	{
	  tree value;
	  enum tree_code comp_code;

	  /* Mark OP in our live bitmap.  */
	  SET_BIT (live, SSA_NAME_VERSION (op));

	  /* If OP is used in such a way that we can infer a value
	     range for it, and we don't find a previous assertion for
	     it, create a new assertion location node for OP.  */
	  if (infer_value_range (stmt, op, &comp_code, &value))
	    {
	      /* If we are able to infer a nonzero value range for OP,
		 then walk backwards through the use-def chain to see if OP
		 was set via a typecast.

		 If so, then we can also infer a nonzero value range
		 for the operand of the NOP_EXPR.  */
	      if (comp_code == NE_EXPR && integer_zerop (value))
		{
		  tree t = op;
        GIMPLE_type def_stmt = SSA_NAME_DEF_STMT (t);

		  while (is_gimple_assign (def_stmt)
			 && gimple_assign_rhs_code (def_stmt)  == NOP_EXPR
			 && TREE_CODE
			     (gimple_assign_rhs1 (def_stmt)) == SSA_NAME
			 && POINTER_TYPE_P
			     (TREE_TYPE (gimple_assign_rhs1 (def_stmt))))
		    {
		      t = gimple_assign_rhs1 (def_stmt);
		      def_stmt = SSA_NAME_DEF_STMT (t);

		      /* Note we want to register the assert for the
			 operand of the NOP_EXPR after SI, not after the
			 conversion.  */
		      if (! has_single_use (t))
			{
			  register_new_assert_for (t, t, comp_code, value,
						   bb, NULL, si);
			  need_assert = true;
			}
		    }
		}

	      /* If OP is used only once, namely in this STMT, don't
		 bother creating an ASSERT_EXPR for it.  Such an
		 ASSERT_EXPR would do nothing but increase compile time.  */
	      if (!has_single_use (op))
		{
		  register_new_assert_for (op, op, comp_code, value,
					   bb, NULL, si);
		  need_assert = true;
		}
	    }
	}
    }

  /* Traverse all PHI nodes in BB marking used operands.  */
  for (si = gsi_start_phis (bb); !gsi_end_p(si); gsi_next (&si))
    {
      use_operand_p arg_p;
      ssa_op_iter i;
      phi = gsi_stmt (si);

      FOR_EACH_PHI_ARG (arg_p, phi, i, SSA_OP_USE)
	{
	  tree arg = USE_FROM_PTR (arg_p);
	  if (TREE_CODE (arg) == SSA_NAME)
	    SET_BIT (live, SSA_NAME_VERSION (arg));
	}
    }

  return need_assert;
}

/* Do an RPO walk over the function computing SSA name liveness
   on-the-fly and deciding on assert expressions to insert.
   Returns true if there are assert expressions to be inserted.  */

static bool
find_assert_locations (void)
{
  int *rpo = XCNEWVEC (int, last_basic_block + NUM_FIXED_BLOCKS);
  int *bb_rpo = XCNEWVEC (int, last_basic_block + NUM_FIXED_BLOCKS);
  int *last_rpo = XCNEWVEC (int, last_basic_block + NUM_FIXED_BLOCKS);
  int rpo_cnt, i;
  bool need_asserts;

  live = XCNEWVEC (sbitmap, last_basic_block + NUM_FIXED_BLOCKS);
  rpo_cnt = pre_and_rev_post_order_compute (NULL, rpo, false);
  for (i = 0; i < rpo_cnt; ++i)
    bb_rpo[rpo[i]] = i;

  need_asserts = false;
  for (i = rpo_cnt-1; i >= 0; --i)
    {
      basic_block bb = BASIC_BLOCK (rpo[i]);
      edge e;
      edge_iterator ei;

      if (!live[rpo[i]])
	{
	  live[rpo[i]] = sbitmap_alloc (num_ssa_names);
	  sbitmap_zero (live[rpo[i]]);
	}

      /* Process BB and update the live information with uses in
         this block.  */
      need_asserts |= find_assert_locations_1 (bb, live[rpo[i]]);

      /* Merge liveness into the predecessor blocks and free it.  */
      if (!sbitmap_empty_p (live[rpo[i]]))
	{
	  int pred_rpo = i;
	  FOR_EACH_EDGE (e, ei, bb->preds)
	    {
	      int pred = e->src->index;
	      if (e->flags & EDGE_DFS_BACK)
		continue;

	      if (!live[pred])
		{
		  live[pred] = sbitmap_alloc (num_ssa_names);
		  sbitmap_zero (live[pred]);
		}
	      sbitmap_a_or_b (live[pred], live[pred], live[rpo[i]]);

	      if (bb_rpo[pred] < pred_rpo)
		pred_rpo = bb_rpo[pred];
	    }

	  /* Record the RPO number of the last visited block that needs
	     live information from this block.  */
	  last_rpo[rpo[i]] = pred_rpo;
	}
      else
	{
	  sbitmap_free (live[rpo[i]]);
	  live[rpo[i]] = NULL;
	}

      /* We can free all successors live bitmaps if all their
         predecessors have been visited already.  */
      FOR_EACH_EDGE (e, ei, bb->succs)
	if (last_rpo[e->dest->index] == i
	    && live[e->dest->index])
	  {
	    sbitmap_free (live[e->dest->index]);
	    live[e->dest->index] = NULL;
	  }
    }

  XDELETEVEC (rpo);
  XDELETEVEC (bb_rpo);
  XDELETEVEC (last_rpo);
  for (i = 0; i < last_basic_block + NUM_FIXED_BLOCKS; ++i)
    if (live[i])
      sbitmap_free (live[i]);
  XDELETEVEC (live);

  return need_asserts;
}

/* Create an ASSERT_EXPR for NAME and insert it in the location
   indicated by LOC.  Return true if we made any edge insertions.  */

static bool
process_assert_insertions_for (tree name, assert_locus_t loc)
{
  /* Build the comparison expression NAME_i COMP_CODE VAL.  */
  GIMPLE_type stmt;
  tree cond;
  GIMPLE_type assert_stmt;
  edge_iterator ei;
  edge e;

  /* If we have X <=> X do not insert an assert expr for that.  */
  if (loc->expr == loc->val)
    return false;

  cond = build2 (loc->comp_code, boolean_type_node, loc->expr, loc->val);
  assert_stmt = build_assert_expr_for (cond, name);
  if (loc->e)
    {
      /* We have been asked to insert the assertion on an edge.  This
	 is used only by COND_EXPR and SWITCH_EXPR assertions.  */
      gcc_checking_assert (gimple_code (gsi_stmt (loc->si)) == GIMPLE_COND
			   || (gimple_code (gsi_stmt (loc->si))
			       == GIMPLE_SWITCH));

      gsi_insert_on_edge (loc->e, assert_stmt);
      return true;
    }

  /* Otherwise, we can insert right after LOC->SI iff the
     statement must not be the last statement in the block.  */
  stmt = gsi_stmt (loc->si);
  if (!stmt_ends_bb_p (stmt))
    {
      gsi_insert_after (&loc->si, assert_stmt, GSI_SAME_STMT);
      return false;
    }

  /* If STMT must be the last statement in BB, we can only insert new
     assertions on the non-abnormal edge out of BB.  Note that since
     STMT is not control flow, there may only be one non-abnormal edge
     out of BB.  */
  FOR_EACH_EDGE (e, ei, loc->bb->succs)
    if (!(e->flags & EDGE_ABNORMAL))
      {
	gsi_insert_on_edge (e, assert_stmt);
	return true;
      }

  gcc_unreachable ();
}


/* Process all the insertions registered for every name N_i registered
   in NEED_ASSERT_FOR.  The list of assertions to be inserted are
   found in ASSERTS_FOR[i].  */

static void
process_assert_insertions (void)
{
  unsigned i;
  bitmap_iterator bi;
  bool update_edges_p = false;
  int num_asserts = 0;

  if (dump_file && (dump_flags & TDF_DETAILS))
    dump_all_asserts (dump_file);

  EXECUTE_IF_SET_IN_BITMAP (need_assert_for, 0, i, bi)
    {
      assert_locus_t loc = asserts_for[i];
      gcc_assert (loc);

      while (loc)
	{
	  assert_locus_t next = loc->next;
	  update_edges_p |= process_assert_insertions_for (ssa_name (i), loc);
	  free (loc);
	  loc = next;
	  num_asserts++;
	}
    }

  if (update_edges_p)
    gsi_commit_edge_inserts ();

  statistics_counter_event (cfun, "Number of ASSERT_EXPR expressions inserted",
			    num_asserts);
}


/* Traverse the flowgraph looking for conditional jumps to insert range
   expressions.  These range expressions are meant to provide information
   to optimizations that need to reason in terms of value ranges.  They
   will not be expanded into RTL.  For instance, given:

   x = ...
   y = ...
   if (x < y)
     y = x - 2;
   else
     x = y + 3;

   this pass will transform the code into:

   x = ...
   y = ...
   if (x < y)
    {
      x = ASSERT_EXPR <x, x < y>
      y = x - 2
    }
   else
    {
      y = ASSERT_EXPR <y, x <= y>
      x = y + 3
    }

   The idea is that once copy and constant propagation have run, other
   optimizations will be able to determine what ranges of values can 'x'
   take in different paths of the code, simply by checking the reaching
   definition of 'x'.  */

static void
insert_range_assertions (void)
{
  need_assert_for = BITMAP_ALLOC (NULL);
  asserts_for = XCNEWVEC (assert_locus_t, num_ssa_names);

  calculate_dominance_info (CDI_DOMINATORS);

  if (find_assert_locations ())
    {
      process_assert_insertions ();
      update_ssa (TODO_update_ssa_no_phi);
    }

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "\nSSA form after inserting ASSERT_EXPRs\n");
      dump_function_to_file (current_function_decl, dump_file, dump_flags);
    }

  free (asserts_for);
  BITMAP_FREE (need_assert_for);
}

/* Checks one ARRAY_REF in REF, located at LOCUS. Ignores flexible arrays
   and "struct" hacks. If VRP can determine that the
   array subscript is a constant, check if it is outside valid
   range. If the array subscript is a RANGE, warn if it is
   non-overlapping with valid range.
   IGNORE_OFF_BY_ONE is true if the ARRAY_REF is inside a ADDR_EXPR.  */

static void
check_array_ref (location_t location, tree ref, bool ignore_off_by_one)
{
  value_range_t* vr = NULL;
  tree low_sub, up_sub;
  tree low_bound, up_bound, up_bound_p1;
  tree base;

  if (TREE_NO_WARNING (ref))
    return;

  low_sub = up_sub = TREE_OPERAND (ref, 1);
  up_bound = array_ref_up_bound (ref);

  /* Can not check flexible arrays.  */
  if (!up_bound
      || TREE_CODE (up_bound) != INTEGER_CST)
    return;

  /* Accesses to trailing arrays via pointers may access storage
     beyond the types array bounds.  */
  base = get_base_address (ref);
  if (base && TREE_CODE (base) == MEM_REF)
    {
      tree cref, next = NULL_TREE;

      if (TREE_CODE (TREE_OPERAND (ref, 0)) != COMPONENT_REF)
	return;

      cref = TREE_OPERAND (ref, 0);
      if (TREE_CODE (TREE_TYPE (TREE_OPERAND (cref, 0))) == RECORD_TYPE)
	for (next = DECL_CHAIN (TREE_OPERAND (cref, 1));
	     next && TREE_CODE (next) != FIELD_DECL;
	     next = DECL_CHAIN (next))
	  ;

      /* If this is the last field in a struct type or a field in a
	 union type do not warn.  */
      if (!next)
	return;
    }

  low_bound = array_ref_low_bound (ref);
  up_bound_p1 = int_const_binop (PLUS_EXPR, up_bound, integer_one_node);

  if (TREE_CODE (low_sub) == SSA_NAME)
    {
      vr = get_value_range (low_sub);
      if (vr->type == VR_RANGE || vr->type == VR_ANTI_RANGE)
        {
          low_sub = vr->type == VR_RANGE ? vr->max : vr->min;
          up_sub = vr->type == VR_RANGE ? vr->min : vr->max;
        }
    }

  if (vr && vr->type == VR_ANTI_RANGE)
    {
      if (TREE_CODE (up_sub) == INTEGER_CST
          && tree_int_cst_lt (up_bound, up_sub)
          && TREE_CODE (low_sub) == INTEGER_CST
          && tree_int_cst_lt (low_sub, low_bound))
        {
          warning_at (location, OPT_Warray_bounds,
		      "array subscript is outside array bounds");
          TREE_NO_WARNING (ref) = 1;
        }
    }
  else if (TREE_CODE (up_sub) == INTEGER_CST
	   && (ignore_off_by_one
	       ? (tree_int_cst_lt (up_bound, up_sub)
		  && !tree_int_cst_equal (up_bound_p1, up_sub))
	       : (tree_int_cst_lt (up_bound, up_sub)
		  || tree_int_cst_equal (up_bound_p1, up_sub))))
    {
      warning_at (location, OPT_Warray_bounds,
		  "array subscript is above array bounds");
      TREE_NO_WARNING (ref) = 1;
    }
  else if (TREE_CODE (low_sub) == INTEGER_CST
           && tree_int_cst_lt (low_sub, low_bound))
    {
      warning_at (location, OPT_Warray_bounds,
		  "array subscript is below array bounds");
      TREE_NO_WARNING (ref) = 1;
    }
}

/* Searches if the expr T, located at LOCATION computes
   address of an ARRAY_REF, and call check_array_ref on it.  */

static void
search_for_addr_array (tree t, location_t location)
{
  while (TREE_CODE (t) == SSA_NAME)
    {
      GIMPLE_type g = SSA_NAME_DEF_STMT (t);

      if (gimple_code (g) != GIMPLE_ASSIGN)
	return;

      if (get_gimple_rhs_class (gimple_assign_rhs_code (g))
	  != GIMPLE_SINGLE_RHS)
	return;

      t = gimple_assign_rhs1 (g);
    }


  /* We are only interested in addresses of ARRAY_REF's.  */
  if (TREE_CODE (t) != ADDR_EXPR)
    return;

  /* Check each ARRAY_REFs in the reference chain. */
  do
    {
      if (TREE_CODE (t) == ARRAY_REF)
	check_array_ref (location, t, true /*ignore_off_by_one*/);

      t = TREE_OPERAND (t, 0);
    }
  while (handled_component_p (t));

  if (TREE_CODE (t) == MEM_REF
      && TREE_CODE (TREE_OPERAND (t, 0)) == ADDR_EXPR
      && !TREE_NO_WARNING (t))
    {
      tree tem = TREE_OPERAND (TREE_OPERAND (t, 0), 0);
      tree low_bound, up_bound, el_sz;
      double_int idx;
      if (TREE_CODE (TREE_TYPE (tem)) != ARRAY_TYPE
	  || TREE_CODE (TREE_TYPE (TREE_TYPE (tem))) == ARRAY_TYPE
	  || !TYPE_DOMAIN (TREE_TYPE (tem)))
	return;

      low_bound = TYPE_MIN_VALUE (TYPE_DOMAIN (TREE_TYPE (tem)));
      up_bound = TYPE_MAX_VALUE (TYPE_DOMAIN (TREE_TYPE (tem)));
      el_sz = TYPE_SIZE_UNIT (TREE_TYPE (TREE_TYPE (tem)));
      if (!low_bound
	  || TREE_CODE (low_bound) != INTEGER_CST
	  || !up_bound
	  || TREE_CODE (up_bound) != INTEGER_CST
	  || !el_sz
	  || TREE_CODE (el_sz) != INTEGER_CST)
	return;

      idx = mem_ref_offset (t);
      idx = double_int_sdiv (idx, tree_to_double_int (el_sz), TRUNC_DIV_EXPR);
      if (double_int_scmp (idx, double_int_zero) < 0)
	{
	  warning_at (location, OPT_Warray_bounds,
		      "array subscript is below array bounds");
	  TREE_NO_WARNING (t) = 1;
	}
      else if (double_int_scmp (idx,
				double_int_add
				  (double_int_add
				    (tree_to_double_int (up_bound),
				     double_int_neg
				       (tree_to_double_int (low_bound))),
				    double_int_one)) > 0)
	{
	  warning_at (location, OPT_Warray_bounds,
		      "array subscript is above array bounds");
	  TREE_NO_WARNING (t) = 1;
	}
    }
}

/* walk_tree() callback that checks if *TP is
   an ARRAY_REF inside an ADDR_EXPR (in which an array
   subscript one outside the valid range is allowed). Call
   check_array_ref for each ARRAY_REF found. The location is
   passed in DATA.  */

static tree
check_array_bounds (tree *tp, int *walk_subtree, void *data)
{
  tree t = *tp;
  struct walk_stmt_info *wi = (struct walk_stmt_info *) data;
  location_t location;

  if (EXPR_HAS_LOCATION (t))
    location = EXPR_LOCATION (t);
  else
    {
      location_t *locp = (location_t *) wi->info;
      location = *locp;
    }

  *walk_subtree = TRUE;

  if (TREE_CODE (t) == ARRAY_REF)
    check_array_ref (location, t, false /*ignore_off_by_one*/);

  if (TREE_CODE (t) == MEM_REF
      || (TREE_CODE (t) == RETURN_EXPR && TREE_OPERAND (t, 0)))
    search_for_addr_array (TREE_OPERAND (t, 0), location);

  if (TREE_CODE (t) == ADDR_EXPR)
    *walk_subtree = FALSE;

  return NULL_TREE;
}

/* Walk over all statements of all reachable BBs and call check_array_bounds
   on them.  */

static void
check_all_array_refs (void)
{
  basic_block bb;
  gimple_stmt_iterator si;

  FOR_EACH_BB (bb)
    {
      edge_iterator ei;
      edge e;
      bool executable = false;

      /* Skip blocks that were found to be unreachable.  */
      FOR_EACH_EDGE (e, ei, bb->preds)
	executable |= !!(e->flags & EDGE_EXECUTABLE);
      if (!executable)
	continue;

      for (si = gsi_start_bb (bb); !gsi_end_p (si); gsi_next (&si))
	{
     GIMPLE_type stmt = gsi_stmt (si);
	  struct walk_stmt_info wi;
	  if (!gimple_has_location (stmt))
	    continue;

	  if (is_gimple_call (stmt))
	    {
	      size_t i;
	      size_t n = gimple_call_num_args (stmt);
	      for (i = 0; i < n; i++)
		{
		  tree arg = gimple_call_arg (stmt, i);
		  search_for_addr_array (arg, gimple_location (stmt));
		}
	    }
	  else
	    {
	      memset (&wi, 0, sizeof (wi));
	      wi.info = CONST_CAST (void *, (const void *)
				    gimple_location_ptr (stmt));

	      walk_gimple_op (gsi_stmt (si),
			      check_array_bounds,
			      &wi);
	    }
	}
    }
}

/* Convert range assertion expressions into the implied copies and
   copy propagate away the copies.  Doing the trivial copy propagation
   here avoids the need to run the full copy propagation pass after
   VRP.

   FIXME, this will eventually lead to copy propagation removing the
   names that had useful range information attached to them.  For
   instance, if we had the assertion N_i = ASSERT_EXPR <N_j, N_j > 3>,
   then N_i will have the range [3, +INF].

   However, by converting the assertion into the implied copy
   operation N_i = N_j, we will then copy-propagate N_j into the uses
   of N_i and lose the range information.  We may want to hold on to
   ASSERT_EXPRs a little while longer as the ranges could be used in
   things like jump threading.

   The problem with keeping ASSERT_EXPRs around is that passes after
   VRP need to handle them appropriately.

   Another approach would be to make the range information a first
   class property of the SSA_NAME so that it can be queried from
   any pass.  This is made somewhat more complex by the need for
   multiple ranges to be associated with one SSA_NAME.  */

static void
remove_range_assertions (void)
{
  basic_block bb;
  gimple_stmt_iterator si;

  /* Note that the BSI iterator bump happens at the bottom of the
     loop and no bump is necessary if we're removing the statement
     referenced by the current BSI.  */
  FOR_EACH_BB (bb)
    for (si = gsi_start_bb (bb); !gsi_end_p (si);)
      {
   GIMPLE_type stmt = gsi_stmt (si);
   GIMPLE_type use_stmt;

	if (is_gimple_assign (stmt)
	    && gimple_assign_rhs_code (stmt) == ASSERT_EXPR)
	  {
	    tree rhs = gimple_assign_rhs1 (stmt);
	    tree var;
	    tree cond = fold (ASSERT_EXPR_COND (rhs));
	    use_operand_p use_p;
	    imm_use_iterator iter;

	    gcc_assert (cond != boolean_false_node);

	    /* Propagate the RHS into every use of the LHS.  */
	    var = ASSERT_EXPR_VAR (rhs);
	    FOR_EACH_IMM_USE_STMT (use_stmt, iter,
				   gimple_assign_lhs (stmt))
	      FOR_EACH_IMM_USE_ON_STMT (use_p, iter)
		{
		  SET_USE (use_p, var);
		  gcc_assert (TREE_CODE (var) == SSA_NAME);
		}

	    /* And finally, remove the copy, it is not needed.  */
	    gsi_remove (&si, true);
	    release_defs (stmt);
	  }
	else
	  gsi_next (&si);
      }
}


/* Return true if STMT is interesting for VRP.  */

static bool
stmt_interesting_for_vrp (GIMPLE_type stmt)
{
  if (gimple_code (stmt) == GIMPLE_PHI
      && is_gimple_reg (gimple_phi_result (stmt))
      && (INTEGRAL_TYPE_P (TREE_TYPE (gimple_phi_result (stmt)))
	  || POINTER_TYPE_P (TREE_TYPE (gimple_phi_result (stmt)))))
    return true;
  else if (is_gimple_assign (stmt) || is_gimple_call (stmt))
    {
      tree lhs = gimple_get_lhs (stmt);

      /* In general, assignments with virtual operands are not useful
	 for deriving ranges, with the obvious exception of calls to
	 builtin functions.  */
      if (lhs && TREE_CODE (lhs) == SSA_NAME
	  && (INTEGRAL_TYPE_P (TREE_TYPE (lhs))
	      || POINTER_TYPE_P (TREE_TYPE (lhs)))
	  && ((is_gimple_call (stmt)
	       && gimple_call_fndecl (stmt) != NULL_TREE
	       && DECL_BUILT_IN (gimple_call_fndecl (stmt)))
	      || !gimple_vuse (stmt)))
	return true;
    }
  else if (gimple_code (stmt) == GIMPLE_COND
	   || gimple_code (stmt) == GIMPLE_SWITCH)
    return true;

  return false;
}


/* Initialize local data structures for VRP.  */

static void
vrp_initialize (void)
{
  basic_block bb;

  values_propagated = false;
  num_vr_values = num_ssa_names;
  vr_value = XCNEWVEC (value_range_t *, num_vr_values);
  vr_phi_edge_counts = XCNEWVEC (int, num_ssa_names);

  FOR_EACH_BB (bb)
    {
      gimple_stmt_iterator si;

      for (si = gsi_start_phis (bb); !gsi_end_p (si); gsi_next (&si))
	{
     GIMPLE_type phi = gsi_stmt (si);
	  if (!stmt_interesting_for_vrp (phi))
	    {
	      tree lhs = PHI_RESULT (phi);
	      set_value_range_to_varying (get_value_range (lhs));
	      prop_set_simulate_again (phi, false);
	    }
	  else
	    prop_set_simulate_again (phi, true);
	}

      for (si = gsi_start_bb (bb); !gsi_end_p (si); gsi_next (&si))
        {
     GIMPLE_type stmt = gsi_stmt (si);

 	  /* If the statement is a control insn, then we do not
 	     want to avoid simulating the statement once.  Failure
 	     to do so means that those edges will never get added.  */
	  if (stmt_ends_bb_p (stmt))
	    prop_set_simulate_again (stmt, true);
	  else if (!stmt_interesting_for_vrp (stmt))
	    {
	      ssa_op_iter i;
	      tree def;
	      FOR_EACH_SSA_TREE_OPERAND (def, stmt, i, SSA_OP_DEF)
		set_value_range_to_varying (get_value_range (def));
	      prop_set_simulate_again (stmt, false);
	    }
	  else
	    prop_set_simulate_again (stmt, true);
	}
    }
}

/* Return the singleton value-range for NAME or NAME.  */

static inline tree
vrp_valueize (tree name)
{
  if (TREE_CODE (name) == SSA_NAME)
    {
      value_range_t *vr = get_value_range (name);
      if (vr->type == VR_RANGE
	  && (vr->min == vr->max
	      || operand_equal_p (vr->min, vr->max, 0)))
	return vr->min;
    }
  return name;
}

/* Visit assignment STMT.  If it produces an interesting range, record
   the SSA name in *OUTPUT_P.  */

static enum ssa_prop_result
vrp_visit_assignment_or_call (GIMPLE_type stmt, tree *output_p)
{
  tree def, lhs;
  ssa_op_iter iter;
  enum gimple_code code = gimple_code (stmt);
  lhs = gimple_get_lhs (stmt);

  /* We only keep track of ranges in integral and pointer types.  */
  if (TREE_CODE (lhs) == SSA_NAME
      && ((INTEGRAL_TYPE_P (TREE_TYPE (lhs))
	   /* It is valid to have NULL MIN/MAX values on a type.  See
	      build_range_type.  */
	   && TYPE_MIN_VALUE (TREE_TYPE (lhs))
	   && TYPE_MAX_VALUE (TREE_TYPE (lhs)))
	  || POINTER_TYPE_P (TREE_TYPE (lhs))))
    {
      value_range_t new_vr = { VR_UNDEFINED, NULL_TREE, NULL_TREE, NULL };

      /* Try folding the statement to a constant first.  */
      tree tem = gimple_fold_stmt_to_constant (stmt, vrp_valueize);
      if (tem && !is_overflow_infinity (tem))
	set_value_range (&new_vr, VR_RANGE, tem, tem, NULL);
      /* Then dispatch to value-range extracting functions.  */
      else if (code == GIMPLE_CALL)
	extract_range_basic (&new_vr, stmt);
      else
	extract_range_from_assignment (&new_vr, stmt);

      if (update_value_range (lhs, &new_vr))
	{
	  *output_p = lhs;

	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      fprintf (dump_file, "Found new range for ");
	      print_generic_expr (dump_file, lhs, 0);
	      fprintf (dump_file, ": ");
	      dump_value_range (dump_file, &new_vr);
	      fprintf (dump_file, "\n\n");
	    }

	  if (new_vr.type == VR_VARYING)
	    return SSA_PROP_VARYING;

	  return SSA_PROP_INTERESTING;
	}

      return SSA_PROP_NOT_INTERESTING;
    }

  /* Every other statement produces no useful ranges.  */
  FOR_EACH_SSA_TREE_OPERAND (def, stmt, iter, SSA_OP_DEF)
    set_value_range_to_varying (get_value_range (def));

  return SSA_PROP_VARYING;
}

/* Helper that gets the value range of the SSA_NAME with version I
   or a symbolic range containing the SSA_NAME only if the value range
   is varying or undefined.  */

static inline value_range_t
get_vr_for_comparison (int i)
{
  value_range_t vr = *get_value_range (ssa_name (i));

  /* If name N_i does not have a valid range, use N_i as its own
     range.  This allows us to compare against names that may
     have N_i in their ranges.  */
  if (vr.type == VR_VARYING || vr.type == VR_UNDEFINED)
    {
      vr.type = VR_RANGE;
      vr.min = ssa_name (i);
      vr.max = ssa_name (i);
    }

  return vr;
}

/* Compare all the value ranges for names equivalent to VAR with VAL
   using comparison code COMP.  Return the same value returned by
   compare_range_with_value, including the setting of
   *STRICT_OVERFLOW_P.  */

static tree
compare_name_with_value (enum tree_code comp, tree var, tree val,
			 bool *strict_overflow_p)
{
  bitmap_iterator bi;
  unsigned i;
  bitmap e;
  tree retval, t;
  int used_strict_overflow;
  bool sop;
  value_range_t equiv_vr;

  /* Get the set of equivalences for VAR.  */
  e = get_value_range (var)->equiv;

  /* Start at -1.  Set it to 0 if we do a comparison without relying
     on overflow, or 1 if all comparisons rely on overflow.  */
  used_strict_overflow = -1;

  /* Compare vars' value range with val.  */
  equiv_vr = get_vr_for_comparison (SSA_NAME_VERSION (var));
  sop = false;
  retval = compare_range_with_value (comp, &equiv_vr, val, &sop);
  if (retval)
    used_strict_overflow = sop ? 1 : 0;

  /* If the equiv set is empty we have done all work we need to do.  */
  if (e == NULL)
    {
      if (retval
	  && used_strict_overflow > 0)
	*strict_overflow_p = true;
      return retval;
    }

  EXECUTE_IF_SET_IN_BITMAP (e, 0, i, bi)
    {
      equiv_vr = get_vr_for_comparison (i);
      sop = false;
      t = compare_range_with_value (comp, &equiv_vr, val, &sop);
      if (t)
	{
	  /* If we get different answers from different members
	     of the equivalence set this check must be in a dead
	     code region.  Folding it to a trap representation
	     would be correct here.  For now just return don't-know.  */
	  if (retval != NULL
	      && t != retval)
	    {
	      retval = NULL_TREE;
	      break;
	    }
	  retval = t;

	  if (!sop)
	    used_strict_overflow = 0;
	  else if (used_strict_overflow < 0)
	    used_strict_overflow = 1;
	}
    }

  if (retval
      && used_strict_overflow > 0)
    *strict_overflow_p = true;

  return retval;
}


/* Given a comparison code COMP and names N1 and N2, compare all the
   ranges equivalent to N1 against all the ranges equivalent to N2
   to determine the value of N1 COMP N2.  Return the same value
   returned by compare_ranges.  Set *STRICT_OVERFLOW_P to indicate
   whether we relied on an overflow infinity in the comparison.  */


static tree
compare_names (enum tree_code comp, tree n1, tree n2,
	       bool *strict_overflow_p)
{
  tree t, retval;
  bitmap e1, e2;
  bitmap_iterator bi1, bi2;
  unsigned i1, i2;
  int used_strict_overflow;
  static bitmap_obstack *s_obstack = NULL;
  static bitmap s_e1 = NULL, s_e2 = NULL;

  /* Compare the ranges of every name equivalent to N1 against the
     ranges of every name equivalent to N2.  */
  e1 = get_value_range (n1)->equiv;
  e2 = get_value_range (n2)->equiv;

  /* Use the fake bitmaps if e1 or e2 are not available.  */
  if (s_obstack == NULL)
    {
      s_obstack = XNEW (bitmap_obstack);
      bitmap_obstack_initialize (s_obstack);
      s_e1 = BITMAP_ALLOC (s_obstack);
      s_e2 = BITMAP_ALLOC (s_obstack);
    }
  if (e1 == NULL)
    e1 = s_e1;
  if (e2 == NULL)
    e2 = s_e2;

  /* Add N1 and N2 to their own set of equivalences to avoid
     duplicating the body of the loop just to check N1 and N2
     ranges.  */
  bitmap_set_bit (e1, SSA_NAME_VERSION (n1));
  bitmap_set_bit (e2, SSA_NAME_VERSION (n2));

  /* If the equivalence sets have a common intersection, then the two
     names can be compared without checking their ranges.  */
  if (bitmap_intersect_p (e1, e2))
    {
      bitmap_clear_bit (e1, SSA_NAME_VERSION (n1));
      bitmap_clear_bit (e2, SSA_NAME_VERSION (n2));

      return (comp == EQ_EXPR || comp == GE_EXPR || comp == LE_EXPR)
	     ? boolean_true_node
	     : boolean_false_node;
    }

  /* Start at -1.  Set it to 0 if we do a comparison without relying
     on overflow, or 1 if all comparisons rely on overflow.  */
  used_strict_overflow = -1;

  /* Otherwise, compare all the equivalent ranges.  First, add N1 and
     N2 to their own set of equivalences to avoid duplicating the body
     of the loop just to check N1 and N2 ranges.  */
  EXECUTE_IF_SET_IN_BITMAP (e1, 0, i1, bi1)
    {
      value_range_t vr1 = get_vr_for_comparison (i1);

      t = retval = NULL_TREE;
      EXECUTE_IF_SET_IN_BITMAP (e2, 0, i2, bi2)
	{
	  bool sop = false;

	  value_range_t vr2 = get_vr_for_comparison (i2);

	  t = compare_ranges (comp, &vr1, &vr2, &sop);
	  if (t)
	    {
	      /* If we get different answers from different members
		 of the equivalence set this check must be in a dead
		 code region.  Folding it to a trap representation
		 would be correct here.  For now just return don't-know.  */
	      if (retval != NULL
		  && t != retval)
		{
		  bitmap_clear_bit (e1, SSA_NAME_VERSION (n1));
		  bitmap_clear_bit (e2, SSA_NAME_VERSION (n2));
		  return NULL_TREE;
		}
	      retval = t;

	      if (!sop)
		used_strict_overflow = 0;
	      else if (used_strict_overflow < 0)
		used_strict_overflow = 1;
	    }
	}

      if (retval)
	{
	  bitmap_clear_bit (e1, SSA_NAME_VERSION (n1));
	  bitmap_clear_bit (e2, SSA_NAME_VERSION (n2));
	  if (used_strict_overflow > 0)
	    *strict_overflow_p = true;
	  return retval;
	}
    }

  /* None of the equivalent ranges are useful in computing this
     comparison.  */
  bitmap_clear_bit (e1, SSA_NAME_VERSION (n1));
  bitmap_clear_bit (e2, SSA_NAME_VERSION (n2));
  return NULL_TREE;
}

/* Helper function for vrp_evaluate_conditional_warnv.  */

static tree
vrp_evaluate_conditional_warnv_with_ops_using_ranges (enum tree_code code,
						      tree op0, tree op1,
						      bool * strict_overflow_p)
{
  value_range_t *vr0, *vr1;

  vr0 = (TREE_CODE (op0) == SSA_NAME) ? get_value_range (op0) : NULL;
  vr1 = (TREE_CODE (op1) == SSA_NAME) ? get_value_range (op1) : NULL;

  if (vr0 && vr1)
    return compare_ranges (code, vr0, vr1, strict_overflow_p);
  else if (vr0 && vr1 == NULL)
    return compare_range_with_value (code, vr0, op1, strict_overflow_p);
  else if (vr0 == NULL && vr1)
    return (compare_range_with_value
	    (swap_tree_comparison (code), vr1, op0, strict_overflow_p));
  return NULL;
}

/* Helper function for vrp_evaluate_conditional_warnv. */

static tree
vrp_evaluate_conditional_warnv_with_ops (enum tree_code code, tree op0,
					 tree op1, bool use_equiv_p,
					 bool *strict_overflow_p, bool *only_ranges)
{
  tree ret;
  if (only_ranges)
    *only_ranges = true;

  /* We only deal with integral and pointer types.  */
  if (!INTEGRAL_TYPE_P (TREE_TYPE (op0))
      && !POINTER_TYPE_P (TREE_TYPE (op0)))
    return NULL_TREE;

  if (use_equiv_p)
    {
      if (only_ranges
          && (ret = vrp_evaluate_conditional_warnv_with_ops_using_ranges
	              (code, op0, op1, strict_overflow_p)))
	return ret;
      *only_ranges = false;
      if (TREE_CODE (op0) == SSA_NAME && TREE_CODE (op1) == SSA_NAME)
	return compare_names (code, op0, op1, strict_overflow_p);
      else if (TREE_CODE (op0) == SSA_NAME)
	return compare_name_with_value (code, op0, op1, strict_overflow_p);
      else if (TREE_CODE (op1) == SSA_NAME)
	return (compare_name_with_value
		(swap_tree_comparison (code), op1, op0, strict_overflow_p));
    }
  else
    return vrp_evaluate_conditional_warnv_with_ops_using_ranges (code, op0, op1,
								 strict_overflow_p);
  return NULL_TREE;
}

/* Given (CODE OP0 OP1) within STMT, try to simplify it based on value range
   information.  Return NULL if the conditional can not be evaluated.
   The ranges of all the names equivalent with the operands in COND
   will be used when trying to compute the value.  If the result is
   based on undefined signed overflow, issue a warning if
   appropriate.  */

static tree
vrp_evaluate_conditional (enum tree_code code, tree op0, tree op1, GIMPLE_type stmt)
{
  bool sop;
  tree ret;
  bool only_ranges;

  /* Some passes and foldings leak constants with overflow flag set
     into the IL.  Avoid doing wrong things with these and bail out.  */
  if ((TREE_CODE (op0) == INTEGER_CST
       && TREE_OVERFLOW (op0))
      || (TREE_CODE (op1) == INTEGER_CST
	  && TREE_OVERFLOW (op1)))
    return NULL_TREE;

  sop = false;
  ret = vrp_evaluate_conditional_warnv_with_ops (code, op0, op1, true, &sop,
  						 &only_ranges);

  if (ret && sop)
    {
      enum warn_strict_overflow_code wc;
      const char* warnmsg;

      if (is_gimple_min_invariant (ret))
	{
	  wc = WARN_STRICT_OVERFLOW_CONDITIONAL;
	  warnmsg = G_("assuming signed overflow does not occur when "
		       "simplifying conditional to constant");
	}
      else
	{
	  wc = WARN_STRICT_OVERFLOW_COMPARISON;
	  warnmsg = G_("assuming signed overflow does not occur when "
		       "simplifying conditional");
	}

      if (issue_strict_overflow_warning (wc))
	{
	  location_t location;

	  if (!gimple_has_location (stmt))
	    location = input_location;
	  else
	    location = gimple_location (stmt);
	  warning_at (location, OPT_Wstrict_overflow, "%s", warnmsg);
	}
    }

  if (warn_type_limits
      && ret && only_ranges
      && TREE_CODE_CLASS (code) == tcc_comparison
      && TREE_CODE (op0) == SSA_NAME)
    {
      /* If the comparison is being folded and the operand on the LHS
	 is being compared against a constant value that is outside of
	 the natural range of OP0's type, then the predicate will
	 always fold regardless of the value of OP0.  If -Wtype-limits
	 was specified, emit a warning.  */
      tree type = TREE_TYPE (op0);
      value_range_t *vr0 = get_value_range (op0);

      if (vr0->type != VR_VARYING
	  && INTEGRAL_TYPE_P (type)
	  && vrp_val_is_min (vr0->min)
	  && vrp_val_is_max (vr0->max)
	  && is_gimple_min_invariant (op1))
	{
	  location_t location;

	  if (!gimple_has_location (stmt))
	    location = input_location;
	  else
	    location = gimple_location (stmt);

	  warning_at (location, OPT_Wtype_limits,
		      integer_zerop (ret)
		      ? G_("comparison always false "
                           "due to limited range of data type")
		      : G_("comparison always true "
                           "due to limited range of data type"));
	}
    }

  return ret;
}


/* Visit conditional statement STMT.  If we can determine which edge
   will be taken out of STMT's basic block, record it in
   *TAKEN_EDGE_P and return SSA_PROP_INTERESTING.  Otherwise, return
   SSA_PROP_VARYING.  */

static enum ssa_prop_result
vrp_visit_cond_stmt (GIMPLE_type stmt, edge *taken_edge_p)
{
  tree val;
  bool sop;

  *taken_edge_p = NULL;

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      tree use;
      ssa_op_iter i;

      fprintf (dump_file, "\nVisiting conditional with predicate: ");
      print_gimple_stmt (dump_file, stmt, 0, 0);
      fprintf (dump_file, "\nWith known ranges\n");

      FOR_EACH_SSA_TREE_OPERAND (use, stmt, i, SSA_OP_USE)
	{
	  fprintf (dump_file, "\t");
	  print_generic_expr (dump_file, use, 0);
	  fprintf (dump_file, ": ");
	  dump_value_range (dump_file, vr_value[SSA_NAME_VERSION (use)]);
	}

      fprintf (dump_file, "\n");
    }

  /* Compute the value of the predicate COND by checking the known
     ranges of each of its operands.

     Note that we cannot evaluate all the equivalent ranges here
     because those ranges may not yet be final and with the current
     propagation strategy, we cannot determine when the value ranges
     of the names in the equivalence set have changed.

     For instance, given the following code fragment

        i_5 = PHI <8, i_13>
	...
     	i_14 = ASSERT_EXPR <i_5, i_5 != 0>
	if (i_14 == 1)
	  ...

     Assume that on the first visit to i_14, i_5 has the temporary
     range [8, 8] because the second argument to the PHI function is
     not yet executable.  We derive the range ~[0, 0] for i_14 and the
     equivalence set { i_5 }.  So, when we visit 'if (i_14 == 1)' for
     the first time, since i_14 is equivalent to the range [8, 8], we
     determine that the predicate is always false.

     On the next round of propagation, i_13 is determined to be
     VARYING, which causes i_5 to drop down to VARYING.  So, another
     visit to i_14 is scheduled.  In this second visit, we compute the
     exact same range and equivalence set for i_14, namely ~[0, 0] and
     { i_5 }.  But we did not have the previous range for i_5
     registered, so vrp_visit_assignment thinks that the range for
     i_14 has not changed.  Therefore, the predicate 'if (i_14 == 1)'
     is not visited again, which stops propagation from visiting
     statements in the THEN clause of that if().

     To properly fix this we would need to keep the previous range
     value for the names in the equivalence set.  This way we would've
     discovered that from one visit to the other i_5 changed from
     range [8, 8] to VR_VARYING.

     However, fixing this apparent limitation may not be worth the
     additional checking.  Testing on several code bases (GCC, DLV,
     MICO, TRAMP3D and SPEC2000) showed that doing this results in
     4 more predicates folded in SPEC.  */
  sop = false;

  val = vrp_evaluate_conditional_warnv_with_ops (gimple_cond_code (stmt),
						 gimple_cond_lhs (stmt),
						 gimple_cond_rhs (stmt),
						 false, &sop, NULL);
  if (val)
    {
      if (!sop)
	*taken_edge_p = find_taken_edge (gimple_bb (stmt), val);
      else
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file,
		     "\nIgnoring predicate evaluation because "
		     "it assumes that signed overflow is undefined");
	  val = NULL_TREE;
	}
    }

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "\nPredicate evaluates to: ");
      if (val == NULL_TREE)
	fprintf (dump_file, "DON'T KNOW\n");
      else
	print_generic_stmt (dump_file, val, 0);
    }

  return (*taken_edge_p) ? SSA_PROP_INTERESTING : SSA_PROP_VARYING;
}

/* Searches the case label vector VEC for the index *IDX of the CASE_LABEL
   that includes the value VAL.  The search is restricted to the range
   [START_IDX, n - 1] where n is the size of VEC.

   If there is a CASE_LABEL for VAL, its index is placed in IDX and true is
   returned.

   If there is no CASE_LABEL for VAL and there is one that is larger than VAL,
   it is placed in IDX and false is returned.

   If VAL is larger than any CASE_LABEL, n is placed on IDX and false is
   returned. */

static bool
find_case_label_index (GIMPLE_type stmt, size_t start_idx, tree val, size_t *idx)
{
  size_t n = gimple_switch_num_labels (stmt);
  size_t low, high;

  /* Find case label for minimum of the value range or the next one.
     At each iteration we are searching in [low, high - 1]. */

  for (low = start_idx, high = n; high != low; )
    {
      tree t;
      int cmp;
      /* Note that i != high, so we never ask for n. */
      size_t i = (high + low) / 2;
      t = gimple_switch_label (stmt, i);

      /* Cache the result of comparing CASE_LOW and val.  */
      cmp = tree_int_cst_compare (CASE_LOW (t), val);

      if (cmp == 0)
	{
	  /* Ranges cannot be empty. */
	  *idx = i;
	  return true;
	}
      else if (cmp > 0)
        high = i;
      else
	{
	  low = i + 1;
	  if (CASE_HIGH (t) != NULL
	      && tree_int_cst_compare (CASE_HIGH (t), val) >= 0)
	    {
	      *idx = i;
	      return true;
	    }
        }
    }

  *idx = high;
  return false;
}

/* Searches the case label vector VEC for the range of CASE_LABELs that is used
   for values between MIN and MAX. The first index is placed in MIN_IDX. The
   last index is placed in MAX_IDX. If the range of CASE_LABELs is empty
   then MAX_IDX < MIN_IDX.
   Returns true if the default label is not needed. */

static bool
find_case_label_range (GIMPLE_type stmt, tree min, tree max, size_t *min_idx,
		       size_t *max_idx)
{
  size_t i, j;
  bool min_take_default = !find_case_label_index (stmt, 1, min, &i);
  bool max_take_default = !find_case_label_index (stmt, i, max, &j);

  if (i == j
      && min_take_default
      && max_take_default)
    {
      /* Only the default case label reached.
         Return an empty range. */
      *min_idx = 1;
      *max_idx = 0;
      return false;
    }
  else
    {
      bool take_default = min_take_default || max_take_default;
      tree low, high;
      size_t k;

      if (max_take_default)
	j--;

      /* If the case label range is continuous, we do not need
	 the default case label.  Verify that.  */
      high = CASE_LOW (gimple_switch_label (stmt, i));
      if (CASE_HIGH (gimple_switch_label (stmt, i)))
	high = CASE_HIGH (gimple_switch_label (stmt, i));
      for (k = i + 1; k <= j; ++k)
	{
	  low = CASE_LOW (gimple_switch_label (stmt, k));
	  if (!integer_onep (int_const_binop (MINUS_EXPR, low, high)))
	    {
	      take_default = true;
	      break;
	    }
	  high = low;
	  if (CASE_HIGH (gimple_switch_label (stmt, k)))
	    high = CASE_HIGH (gimple_switch_label (stmt, k));
	}

      *min_idx = i;
      *max_idx = j;
      return !take_default;
    }
}

/* Visit switch statement STMT.  If we can determine which edge
   will be taken out of STMT's basic block, record it in
   *TAKEN_EDGE_P and return SSA_PROP_INTERESTING.  Otherwise, return
   SSA_PROP_VARYING.  */

static enum ssa_prop_result
vrp_visit_switch_stmt (GIMPLE_type stmt, edge *taken_edge_p)
{
  tree op, val;
  value_range_t *vr;
  size_t i = 0, j = 0;
  bool take_default;

  *taken_edge_p = NULL;
  op = gimple_switch_index (stmt);
  if (TREE_CODE (op) != SSA_NAME)
    return SSA_PROP_VARYING;

  vr = get_value_range (op);
  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "\nVisiting switch expression with operand ");
      print_generic_expr (dump_file, op, 0);
      fprintf (dump_file, " with known range ");
      dump_value_range (dump_file, vr);
      fprintf (dump_file, "\n");
    }

  if (vr->type != VR_RANGE
      || symbolic_range_p (vr))
    return SSA_PROP_VARYING;

  /* Find the single edge that is taken from the switch expression.  */
  take_default = !find_case_label_range (stmt, vr->min, vr->max, &i, &j);

  /* Check if the range spans no CASE_LABEL. If so, we only reach the default
     label */
  if (j < i)
    {
      gcc_assert (take_default);
      val = gimple_switch_default_label (stmt);
    }
  else
    {
      /* Check if labels with index i to j and maybe the default label
	 are all reaching the same label.  */

      val = gimple_switch_label (stmt, i);
      if (take_default
	  && CASE_LABEL (gimple_switch_default_label (stmt))
	  != CASE_LABEL (val))
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file, "  not a single destination for this "
		     "range\n");
          return SSA_PROP_VARYING;
	}
      for (++i; i <= j; ++i)
        {
          if (CASE_LABEL (gimple_switch_label (stmt, i)) != CASE_LABEL (val))
	    {
	      if (dump_file && (dump_flags & TDF_DETAILS))
		fprintf (dump_file, "  not a single destination for this "
			 "range\n");
	      return SSA_PROP_VARYING;
	    }
        }
    }

  *taken_edge_p = find_edge (gimple_bb (stmt),
			     label_to_block (CASE_LABEL (val)));

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "  will take edge to ");
      print_generic_stmt (dump_file, CASE_LABEL (val), 0);
    }

  return SSA_PROP_INTERESTING;
}


/* Evaluate statement STMT.  If the statement produces a useful range,
   return SSA_PROP_INTERESTING and record the SSA name with the
   interesting range into *OUTPUT_P.

   If STMT is a conditional branch and we can determine its truth
   value, the taken edge is recorded in *TAKEN_EDGE_P.

   If STMT produces a varying value, return SSA_PROP_VARYING.  */

static enum ssa_prop_result
vrp_visit_stmt (GIMPLE_type stmt, edge *taken_edge_p, tree *output_p)
{
  tree def;
  ssa_op_iter iter;

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "\nVisiting statement:\n");
      print_gimple_stmt (dump_file, stmt, 0, dump_flags);
      fprintf (dump_file, "\n");
    }

  if (!stmt_interesting_for_vrp (stmt))
    gcc_assert (stmt_ends_bb_p (stmt));
  else if (is_gimple_assign (stmt) || is_gimple_call (stmt))
    {
      /* In general, assignments with virtual operands are not useful
	 for deriving ranges, with the obvious exception of calls to
	 builtin functions.  */
      if ((is_gimple_call (stmt)
	   && gimple_call_fndecl (stmt) != NULL_TREE
	   && DECL_BUILT_IN (gimple_call_fndecl (stmt)))
	  || !gimple_vuse (stmt))
	return vrp_visit_assignment_or_call (stmt, output_p);
    }
  else if (gimple_code (stmt) == GIMPLE_COND)
    return vrp_visit_cond_stmt (stmt, taken_edge_p);
  else if (gimple_code (stmt) == GIMPLE_SWITCH)
    return vrp_visit_switch_stmt (stmt, taken_edge_p);

  /* All other statements produce nothing of interest for VRP, so mark
     their outputs varying and prevent further simulation.  */
  FOR_EACH_SSA_TREE_OPERAND (def, stmt, iter, SSA_OP_DEF)
    set_value_range_to_varying (get_value_range (def));

  return SSA_PROP_VARYING;
}


/* Meet operation for value ranges.  Given two value ranges VR0 and
   VR1, store in VR0 a range that contains both VR0 and VR1.  This
   may not be the smallest possible such range.  */

static void
vrp_meet (value_range_t *vr0, value_range_t *vr1)
{
  if (vr0->type == VR_UNDEFINED)
    {
      /* Drop equivalences.  See PR53465.  */
      set_value_range (vr0, vr1->type, vr1->min, vr1->max, NULL);
      return;
    }

  if (vr1->type == VR_UNDEFINED)
    {
      /* VR0 already has the resulting range, just drop equivalences.
	 See PR53465.  */
      if (vr0->equiv)
	bitmap_clear (vr0->equiv);
      return;
    }

  if (vr0->type == VR_VARYING)
    {
      /* Nothing to do.  VR0 already has the resulting range.  */
      return;
    }

  if (vr1->type == VR_VARYING)
    {
      set_value_range_to_varying (vr0);
      return;
    }

  if (vr0->type == VR_RANGE && vr1->type == VR_RANGE)
    {
      int cmp;
      tree min, max;

      /* Compute the convex hull of the ranges.  The lower limit of
         the new range is the minimum of the two ranges.  If they
	 cannot be compared, then give up.  */
      cmp = compare_values (vr0->min, vr1->min);
      if (cmp == 0 || cmp == 1)
        min = vr1->min;
      else if (cmp == -1)
        min = vr0->min;
      else
	goto give_up;

      /* Similarly, the upper limit of the new range is the maximum
         of the two ranges.  If they cannot be compared, then
	 give up.  */
      cmp = compare_values (vr0->max, vr1->max);
      if (cmp == 0 || cmp == -1)
        max = vr1->max;
      else if (cmp == 1)
        max = vr0->max;
      else
	goto give_up;

      /* Check for useless ranges.  */
      if (INTEGRAL_TYPE_P (TREE_TYPE (min))
	  && ((vrp_val_is_min (min) || is_overflow_infinity (min))
	      && (vrp_val_is_max (max) || is_overflow_infinity (max))))
	goto give_up;

      /* The resulting set of equivalences is the intersection of
	 the two sets.  */
      if (vr0->equiv && vr1->equiv && vr0->equiv != vr1->equiv)
        bitmap_and_into (vr0->equiv, vr1->equiv);
      else if (vr0->equiv && !vr1->equiv)
        bitmap_clear (vr0->equiv);

      set_value_range (vr0, vr0->type, min, max, vr0->equiv);
    }
  else if (vr0->type == VR_ANTI_RANGE && vr1->type == VR_ANTI_RANGE)
    {
      /* Two anti-ranges meet only if their complements intersect.
         Only handle the case of identical ranges.  */
      if (compare_values (vr0->min, vr1->min) == 0
	  && compare_values (vr0->max, vr1->max) == 0
	  && compare_values (vr0->min, vr0->max) == 0)
	{
	  /* The resulting set of equivalences is the intersection of
	     the two sets.  */
	  if (vr0->equiv && vr1->equiv && vr0->equiv != vr1->equiv)
	    bitmap_and_into (vr0->equiv, vr1->equiv);
	  else if (vr0->equiv && !vr1->equiv)
	    bitmap_clear (vr0->equiv);
	}
      else
	goto give_up;
    }
  else if (vr0->type == VR_ANTI_RANGE || vr1->type == VR_ANTI_RANGE)
    {
      /* For a numeric range [VAL1, VAL2] and an anti-range ~[VAL3, VAL4],
         only handle the case where the ranges have an empty intersection.
	 The result of the meet operation is the anti-range.  */
      if (!symbolic_range_p (vr0)
	  && !symbolic_range_p (vr1)
	  && !value_ranges_intersect_p (vr0, vr1))
	{
	  /* Copy most of VR1 into VR0.  Don't copy VR1's equivalence
	     set.  We need to compute the intersection of the two
	     equivalence sets.  */
	  if (vr1->type == VR_ANTI_RANGE)
	    set_value_range (vr0, vr1->type, vr1->min, vr1->max, vr0->equiv);

	  /* The resulting set of equivalences is the intersection of
	     the two sets.  */
	  if (vr0->equiv && vr1->equiv && vr0->equiv != vr1->equiv)
	    bitmap_and_into (vr0->equiv, vr1->equiv);
	  else if (vr0->equiv && !vr1->equiv)
	    bitmap_clear (vr0->equiv);
	}
      else
	goto give_up;
    }
  else
    gcc_unreachable ();

  return;

give_up:
  /* Failed to find an efficient meet.  Before giving up and setting
     the result to VARYING, see if we can at least derive a useful
     anti-range.  FIXME, all this nonsense about distinguishing
     anti-ranges from ranges is necessary because of the odd
     semantics of range_includes_zero_p and friends.  */
  if (!symbolic_range_p (vr0)
      && ((vr0->type == VR_RANGE && !range_includes_zero_p (vr0))
	  || (vr0->type == VR_ANTI_RANGE && range_includes_zero_p (vr0)))
      && !symbolic_range_p (vr1)
      && ((vr1->type == VR_RANGE && !range_includes_zero_p (vr1))
	  || (vr1->type == VR_ANTI_RANGE && range_includes_zero_p (vr1))))
    {
      set_value_range_to_nonnull (vr0, TREE_TYPE (vr0->min));

      /* Since this meet operation did not result from the meeting of
	 two equivalent names, VR0 cannot have any equivalences.  */
      if (vr0->equiv)
	bitmap_clear (vr0->equiv);
    }
  else
    set_value_range_to_varying (vr0);
}


/* Visit all arguments for PHI node PHI that flow through executable
   edges.  If a valid value range can be derived from all the incoming
   value ranges, set a new range for the LHS of PHI.  */

static enum ssa_prop_result
vrp_visit_phi_node (GIMPLE_type phi)
{
  size_t i;
  tree lhs = PHI_RESULT (phi);
  value_range_t *lhs_vr = get_value_range (lhs);
  value_range_t vr_result = { VR_UNDEFINED, NULL_TREE, NULL_TREE, NULL };
  bool first = true;
  int edges, old_edges;
  struct loop *l;

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "\nVisiting PHI node: ");
      print_gimple_stmt (dump_file, phi, 0, dump_flags);
    }

  edges = 0;
  for (i = 0; i < gimple_phi_num_args (phi); i++)
    {
      edge e = gimple_phi_arg_edge (phi, i);

      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file,
	      "\n    Argument #%d (%d -> %d %sexecutable)\n",
	      (int) i, e->src->index, e->dest->index,
	      (e->flags & EDGE_EXECUTABLE) ? "" : "not ");
	}

      if (e->flags & EDGE_EXECUTABLE)
	{
	  tree arg = PHI_ARG_DEF (phi, i);
	  value_range_t vr_arg;

	  ++edges;

	  if (TREE_CODE (arg) == SSA_NAME)
	    {
	      vr_arg = *(get_value_range (arg));
	    }
	  else
	    {
	      if (is_overflow_infinity (arg))
		{
		  arg = copy_node (arg);
		  TREE_OVERFLOW (arg) = 0;
		}

	      vr_arg.type = VR_RANGE;
	      vr_arg.min = arg;
	      vr_arg.max = arg;
	      vr_arg.equiv = NULL;
	    }

	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      fprintf (dump_file, "\t");
	      print_generic_expr (dump_file, arg, dump_flags);
	      fprintf (dump_file, "\n\tValue: ");
	      dump_value_range (dump_file, &vr_arg);
	      fprintf (dump_file, "\n");
	    }

	  if (first)
	    copy_value_range (&vr_result, &vr_arg);
	  else
	    vrp_meet (&vr_result, &vr_arg);
	  first = false;

	  if (vr_result.type == VR_VARYING)
	    break;
	}
    }

  if (vr_result.type == VR_VARYING)
    goto varying;
  else if (vr_result.type == VR_UNDEFINED)
    goto update_range;

  old_edges = vr_phi_edge_counts[SSA_NAME_VERSION (lhs)];
  vr_phi_edge_counts[SSA_NAME_VERSION (lhs)] = edges;

  /* To prevent infinite iterations in the algorithm, derive ranges
     when the new value is slightly bigger or smaller than the
     previous one.  We don't do this if we have seen a new executable
     edge; this helps us avoid an overflow infinity for conditionals
     which are not in a loop.  */
  if (edges > 0
      && gimple_phi_num_args (phi) > 1
      && edges == old_edges)
    {
      int cmp_min = compare_values (lhs_vr->min, vr_result.min);
      int cmp_max = compare_values (lhs_vr->max, vr_result.max);

      /* For non VR_RANGE or for pointers fall back to varying if
	 the range changed.  */
      if ((lhs_vr->type != VR_RANGE || vr_result.type != VR_RANGE
	   || POINTER_TYPE_P (TREE_TYPE (lhs)))
	  && (cmp_min != 0 || cmp_max != 0))
	goto varying;

      /* If the new minimum is smaller or larger than the previous
	 one, go all the way to -INF.  In the first case, to avoid
	 iterating millions of times to reach -INF, and in the
	 other case to avoid infinite bouncing between different
	 minimums.  */
      if (cmp_min > 0 || cmp_min < 0)
	{
	  if (!needs_overflow_infinity (TREE_TYPE (vr_result.min))
	      || !vrp_var_may_overflow (lhs, phi))
	    vr_result.min = TYPE_MIN_VALUE (TREE_TYPE (vr_result.min));
	  else if (supports_overflow_infinity (TREE_TYPE (vr_result.min)))
	    vr_result.min =
		negative_overflow_infinity (TREE_TYPE (vr_result.min));
	}

      /* Similarly, if the new maximum is smaller or larger than
	 the previous one, go all the way to +INF.  */
      if (cmp_max < 0 || cmp_max > 0)
	{
	  if (!needs_overflow_infinity (TREE_TYPE (vr_result.max))
	      || !vrp_var_may_overflow (lhs, phi))
	    vr_result.max = TYPE_MAX_VALUE (TREE_TYPE (vr_result.max));
	  else if (supports_overflow_infinity (TREE_TYPE (vr_result.max)))
	    vr_result.max =
		positive_overflow_infinity (TREE_TYPE (vr_result.max));
	}

      /* If we dropped either bound to +-INF then if this is a loop
	 PHI node SCEV may known more about its value-range.  */
      if ((cmp_min > 0 || cmp_min < 0
	   || cmp_max < 0 || cmp_max > 0)
	  && current_loops
	  && (l = loop_containing_stmt (phi))
	  && l->header == gimple_bb (phi))
	adjust_range_with_scev (&vr_result, l, phi, lhs);

      /* If we will end up with a (-INF, +INF) range, set it to
	 VARYING.  Same if the previous max value was invalid for
	 the type and we end up with vr_result.min > vr_result.max.  */
      if ((vrp_val_is_max (vr_result.max)
	   && vrp_val_is_min (vr_result.min))
	  || compare_values (vr_result.min,
			     vr_result.max) > 0)
	goto varying;
    }

  /* If the new range is different than the previous value, keep
     iterating.  */
update_range:
  if (update_value_range (lhs, &vr_result))
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file, "Found new range for ");
	  print_generic_expr (dump_file, lhs, 0);
	  fprintf (dump_file, ": ");
	  dump_value_range (dump_file, &vr_result);
	  fprintf (dump_file, "\n\n");
	}

      return SSA_PROP_INTERESTING;
    }

  /* Nothing changed, don't add outgoing edges.  */
  return SSA_PROP_NOT_INTERESTING;

  /* No match found.  Set the LHS to VARYING.  */
varying:
  set_value_range_to_varying (lhs_vr);
  return SSA_PROP_VARYING;
}

#else
/* Return the maximum value for TYPE.  */

static inline tree
vrp_val_max (const_tree type)
{
  if (!INTEGRAL_TYPE_P (type))
    return NULL_TREE;

  return TYPE_MAX_VALUE (type);
}

/* Return the minimum value for TYPE.  */

static inline tree
vrp_val_min (const_tree type)
{
  if (!INTEGRAL_TYPE_P (type))
    return NULL_TREE;

  return TYPE_MIN_VALUE (type);
}

/* Return whether VAL is equal to the maximum value of its type.  This
   will be true for a positive overflow infinity.  We can't do a
   simple equality comparison with TYPE_MAX_VALUE because C typedefs
   and Ada subtypes can produce types whose TYPE_MAX_VALUE is not ==
   to the integer constant with the same value in the type.  */

static inline bool
vrp_val_is_max (const_tree val)
{
  tree type_max = vrp_val_max (TREE_TYPE (val));
  return (val == type_max
	  || (type_max != NULL_TREE
	      && operand_equal_p (val, type_max, 0)));
}

/* Return whether VAL is equal to the minimum value of its type.  This
   will be true for a negative overflow infinity.  */

static inline bool
vrp_val_is_min (const_tree val)
{
  tree type_min = vrp_val_min (TREE_TYPE (val));
  return (val == type_min
	  || (type_min != NULL_TREE
	      && operand_equal_p (val, type_min, 0)));
}


/* Return whether TYPE should use an overflow infinity distinct from
   TYPE_{MIN,MAX}_VALUE.  We use an overflow infinity value to
   represent a signed overflow during VRP computations.  An infinity
   is distinct from a half-range, which will go from some number to
   TYPE_{MIN,MAX}_VALUE.  */

static inline bool
needs_overflow_infinity (const_tree type)
{
  return INTEGRAL_TYPE_P (type) && !TYPE_OVERFLOW_WRAPS (type);
}

/* Return whether TYPE can support our overflow infinity
   representation: we use the TREE_OVERFLOW flag, which only exists
   for constants.  If TYPE doesn't support this, we don't optimize
   cases which would require signed overflow--we drop them to
   VARYING.  */

static inline bool
supports_overflow_infinity (const_tree type)
{
  tree min = vrp_val_min (type), max = vrp_val_max (type);
#ifdef ENABLE_CHECKING
  gcc_assert (needs_overflow_infinity (type));
#endif
  return (min != NULL_TREE
	  && CONSTANT_CLASS_P (min)
	  && max != NULL_TREE
	  && CONSTANT_CLASS_P (max));
}

/* VAL is the maximum or minimum value of a type.  Return a
   corresponding overflow infinity.  */

static inline tree
make_overflow_infinity (tree val)
{
  gcc_checking_assert (val != NULL_TREE && CONSTANT_CLASS_P (val));
  val = copy_node (val);
  TREE_OVERFLOW (val) = 1;
  return val;
}

/* Return a negative overflow infinity for TYPE.  */

static inline tree
negative_overflow_infinity (tree type)
{
  gcc_checking_assert (supports_overflow_infinity (type));
  return make_overflow_infinity (vrp_val_min (type));
}

/* Return a positive overflow infinity for TYPE.  */

static inline tree
positive_overflow_infinity (tree type)
{
  gcc_checking_assert (supports_overflow_infinity (type));
  return make_overflow_infinity (vrp_val_max (type));
}

/* Return whether VAL is a negative overflow infinity.  */

static inline bool
is_negative_overflow_infinity (const_tree val)
{
  return (needs_overflow_infinity (TREE_TYPE (val))
	  && CONSTANT_CLASS_P (val)
	  && TREE_OVERFLOW (val)
	  && vrp_val_is_min (val));
}

/* Return whether VAL is a positive overflow infinity.  */

static inline bool
is_positive_overflow_infinity (const_tree val)
{
  return (needs_overflow_infinity (TREE_TYPE (val))
	  && CONSTANT_CLASS_P (val)
	  && TREE_OVERFLOW (val)
	  && vrp_val_is_max (val));
}

/* Return whether VAL is a positive or negative overflow infinity.  */

static inline bool
is_overflow_infinity (const_tree val)
{
  return (needs_overflow_infinity (TREE_TYPE (val))
	  && CONSTANT_CLASS_P (val)
	  && TREE_OVERFLOW (val)
	  && (vrp_val_is_min (val) || vrp_val_is_max (val)));
}

/* Return whether STMT has a constant rhs that is_overflow_infinity. */

static inline bool
stmt_overflow_infinity (GIMPLE_type stmt)
{
  if (is_gimple_assign (stmt)
      && get_gimple_rhs_class (gimple_assign_rhs_code (stmt)) ==
      GIMPLE_SINGLE_RHS)
    return is_overflow_infinity (gimple_assign_rhs1 (stmt));
  return false;
}

/* If VAL is now an overflow infinity, return VAL.  Otherwise, return
   the same value with TREE_OVERFLOW clear.  This can be used to avoid
   confusing a regular value with an overflow value.  */

static inline tree
avoid_overflow_infinity (tree val)
{
  if (!is_overflow_infinity (val))
    return val;

  if (vrp_val_is_max (val))
    return vrp_val_max (TREE_TYPE (val));
  else
    {
      gcc_checking_assert (vrp_val_is_min (val));
      return vrp_val_min (TREE_TYPE (val));
    }
}


/* Return true if ARG is marked with the nonnull attribute in the
   current function signature.  */

static bool
nonnull_arg_p (const_tree arg)
{
  tree t, attrs, fntype;
  unsigned HOST_WIDE_INT arg_num;

  gcc_assert (TREE_CODE (arg) == PARM_DECL && POINTER_TYPE_P (TREE_TYPE (arg)));

  /* The static chain decl is always non null.  */
  if (arg == cfun->static_chain_decl)
    return true;

  fntype = TREE_TYPE (current_function_decl);
  for (attrs = TYPE_ATTRIBUTES (fntype); attrs; attrs = TREE_CHAIN (attrs))
    {
      attrs = lookup_attribute ("nonnull", attrs);

      /* If "nonnull" wasn't specified, we know nothing about the argument.  */
      if (attrs == NULL_TREE)
	return false;

      /* If "nonnull" applies to all the arguments, then ARG is non-null.  */
      if (TREE_VALUE (attrs) == NULL_TREE)
	return true;

      /* Get the position number for ARG in the function signature.  */
      for (arg_num = 1, t = DECL_ARGUMENTS (current_function_decl);
	   t;
	   t = DECL_CHAIN (t), arg_num++)
	{
	  if (t == arg)
	    break;
	}

      gcc_assert (t == arg);

      /* Now see if ARG_NUM is mentioned in the nonnull list.  */
      for (t = TREE_VALUE (attrs); t; t = TREE_CHAIN (t))
	{
	  if (compare_tree_int (TREE_VALUE (t), arg_num) == 0)
	    return true;
	}
    }

  return false;
}


/* Set value range VR to VR_UNDEFINED.  */

static inline void
set_value_range_to_undefined (value_range_t *vr)
{
  vr->type = VR_UNDEFINED;
  vr->min = vr->max = NULL_TREE;
  if (vr->equiv)
    bitmap_clear (vr->equiv);
}


/* Set value range VR to VR_VARYING.  */

static inline void
set_value_range_to_varying (value_range_t *vr)
{
  vr->type = VR_VARYING;
  vr->min = vr->max = NULL_TREE;
  if (vr->equiv)
    bitmap_clear (vr->equiv);
}


/* Set value range VR to {T, MIN, MAX, EQUIV}.  */

static void
set_value_range (value_range_t *vr, enum value_range_type t, tree min,
		 tree max, bitmap equiv)
{
#if defined ENABLE_CHECKING
  /* Check the validity of the range.  */
  if (t == VR_RANGE || t == VR_ANTI_RANGE)
    {
      int cmp;

      gcc_assert (min && max);

      if (INTEGRAL_TYPE_P (TREE_TYPE (min)) && t == VR_ANTI_RANGE)
	gcc_assert (!vrp_val_is_min (min) || !vrp_val_is_max (max));

      cmp = compare_values (min, max);
      gcc_assert (cmp == 0 || cmp == -1 || cmp == -2);

      if (needs_overflow_infinity (TREE_TYPE (min)))
	gcc_assert (!is_overflow_infinity (min)
		    || !is_overflow_infinity (max));
    }

  if (t == VR_UNDEFINED || t == VR_VARYING)
    gcc_assert (min == NULL_TREE && max == NULL_TREE);

  if (t == VR_UNDEFINED || t == VR_VARYING)
    gcc_assert (equiv == NULL || bitmap_empty_p (equiv));
#endif

  vr->type = t;
  vr->min = min;
  vr->max = max;

  /* Since updating the equivalence set involves deep copying the
     bitmaps, only do it if absolutely necessary.  */
  if (vr->equiv == NULL
      && equiv != NULL)
    vr->equiv = BITMAP_ALLOC (NULL);

  if (equiv != vr->equiv)
    {
      if (equiv && !bitmap_empty_p (equiv))
	bitmap_copy (vr->equiv, equiv);
      else
	bitmap_clear (vr->equiv);
    }
}


/* Set value range VR to the canonical form of {T, MIN, MAX, EQUIV}.
   This means adjusting T, MIN and MAX representing the case of a
   wrapping range with MAX < MIN covering [MIN, type_max] U [type_min, MAX]
   as anti-rage ~[MAX+1, MIN-1].  Likewise for wrapping anti-ranges.
   In corner cases where MAX+1 or MIN-1 wraps this will fall back
   to varying.
   This routine exists to ease canonicalization in the case where we
   extract ranges from var + CST op limit.  */

static void
set_and_canonicalize_value_range (value_range_t *vr, enum value_range_type t,
				  tree min, tree max, bitmap equiv)
{
  /* Use the canonical setters for VR_UNDEFINED and VR_VARYING.  */
  if (t == VR_UNDEFINED)
    {
      set_value_range_to_undefined (vr);
      return;
    }
  else if (t == VR_VARYING)
    {
      set_value_range_to_varying (vr);
      return;
    }

  /* Nothing to canonicalize for symbolic ranges.  */
  if (TREE_CODE (min) != INTEGER_CST
      || TREE_CODE (max) != INTEGER_CST)
    {
      set_value_range (vr, t, min, max, equiv);
      return;
    }

  /* Wrong order for min and max, to swap them and the VR type we need
     to adjust them.  */
  if (tree_int_cst_lt (max, min))
    {
      tree one, tmp;

      /* For one bit precision if max < min, then the swapped
	 range covers all values, so for VR_RANGE it is varying and
	 for VR_ANTI_RANGE empty range, so drop to varying as well.  */
      if (TYPE_PRECISION (TREE_TYPE (min)) == 1)
	{
	  set_value_range_to_varying (vr);
	  return;
	}

      one = build_int_cst (TREE_TYPE (min), 1);
      tmp = int_const_binop (PLUS_EXPR, max, one);
      max = int_const_binop (MINUS_EXPR, min, one);
      min = tmp;

      /* There's one corner case, if we had [C+1, C] before we now have
	 that again.  But this represents an empty value range, so drop
	 to varying in this case.  */
      if (tree_int_cst_lt (max, min))
	{
	  set_value_range_to_varying (vr);
	  return;
	}

      t = t == VR_RANGE ? VR_ANTI_RANGE : VR_RANGE;
    }

  /* Anti-ranges that can be represented as ranges should be so.  */
  if (t == VR_ANTI_RANGE)
    {
      bool is_min = vrp_val_is_min (min);
      bool is_max = vrp_val_is_max (max);

      if (is_min && is_max)
	{
	  /* We cannot deal with empty ranges, drop to varying.
	     ???  This could be VR_UNDEFINED instead.  */
	  set_value_range_to_varying (vr);
	  return;
	}
      else if (TYPE_PRECISION (TREE_TYPE (min)) == 1
	       && (is_min || is_max))
	{
	  /* Non-empty boolean ranges can always be represented
	     as a singleton range.  */
	  if (is_min)
	    min = max = vrp_val_max (TREE_TYPE (min));
	  else
	    min = max = vrp_val_min (TREE_TYPE (min));
	  t = VR_RANGE;
	}
      else if (is_min
	       /* As a special exception preserve non-null ranges.  */
	       && !(TYPE_UNSIGNED (TREE_TYPE (min))
		    && integer_zerop (max)))
        {
	  tree one = build_int_cst (TREE_TYPE (max), 1);
	  min = int_const_binop (PLUS_EXPR, max, one);
	  max = vrp_val_max (TREE_TYPE (max));
	  t = VR_RANGE;
        }
      else if (is_max)
        {
	  tree one = build_int_cst (TREE_TYPE (min), 1);
	  max = int_const_binop (MINUS_EXPR, min, one);
	  min = vrp_val_min (TREE_TYPE (min));
	  t = VR_RANGE;
        }
    }

  /* Drop [-INF(OVF), +INF(OVF)] to varying.  */
  if (needs_overflow_infinity (TREE_TYPE (min))
      && is_overflow_infinity (min)
      && is_overflow_infinity (max))
    {
      set_value_range_to_varying (vr);
      return;
    }

  set_value_range (vr, t, min, max, equiv);
}

/* Copy value range FROM into value range TO.  */

static inline void
copy_value_range (value_range_t *to, value_range_t *from)
{
  set_value_range (to, from->type, from->min, from->max, from->equiv);
}

/* Set value range VR to a single value.  This function is only called
   with values we get from statements, and exists to clear the
   TREE_OVERFLOW flag so that we don't think we have an overflow
   infinity when we shouldn't.  */

static inline void
set_value_range_to_value (value_range_t *vr, tree val, bitmap equiv)
{
  gcc_assert (is_gimple_min_invariant (val));
  val = avoid_overflow_infinity (val);
  set_value_range (vr, VR_RANGE, val, val, equiv);
}

/* Set value range VR to a non-negative range of type TYPE.
   OVERFLOW_INFINITY indicates whether to use an overflow infinity
   rather than TYPE_MAX_VALUE; this should be true if we determine
   that the range is nonnegative based on the assumption that signed
   overflow does not occur.  */

static inline void
set_value_range_to_nonnegative (value_range_t *vr, tree type,
				bool overflow_infinity)
{
  tree zero;

  if (overflow_infinity && !supports_overflow_infinity (type))
    {
      set_value_range_to_varying (vr);
      return;
    }

  zero = build_int_cst (type, 0);
  set_value_range (vr, VR_RANGE, zero,
		   (overflow_infinity
		    ? positive_overflow_infinity (type)
		    : TYPE_MAX_VALUE (type)),
		   vr->equiv);
}

/* Set value range VR to a non-NULL range of type TYPE.  */

static inline void
set_value_range_to_nonnull (value_range_t *vr, tree type)
{
  tree zero = build_int_cst (type, 0);
  set_value_range (vr, VR_ANTI_RANGE, zero, zero, vr->equiv);
}


/* Set value range VR to a NULL range of type TYPE.  */

static inline void
set_value_range_to_null (value_range_t *vr, tree type)
{
  set_value_range_to_value (vr, build_int_cst (type, 0), vr->equiv);
}


/* Set value range VR to a range of a truthvalue of type TYPE.  */

static inline void
set_value_range_to_truthvalue (value_range_t *vr, tree type)
{
  if (TYPE_PRECISION (type) == 1)
    set_value_range_to_varying (vr);
  else
    set_value_range (vr, VR_RANGE,
		     build_int_cst (type, 0), build_int_cst (type, 1),
		     vr->equiv);
}


/* If abs (min) < abs (max), set VR to [-max, max], if
   abs (min) >= abs (max), set VR to [-min, min].  */

static void
abs_extent_range (value_range_t *vr, tree min, tree max)
{
  int cmp;

  gcc_assert (TREE_CODE (min) == INTEGER_CST);
  gcc_assert (TREE_CODE (max) == INTEGER_CST);
  gcc_assert (INTEGRAL_TYPE_P (TREE_TYPE (min)));
  gcc_assert (!TYPE_UNSIGNED (TREE_TYPE (min)));
  min = fold_unary (ABS_EXPR, TREE_TYPE (min), min);
  max = fold_unary (ABS_EXPR, TREE_TYPE (max), max);
  if (TREE_OVERFLOW (min) || TREE_OVERFLOW (max))
    {
      set_value_range_to_varying (vr);
      return;
    }
  cmp = compare_values (min, max);
  if (cmp == -1)
    min = fold_unary (NEGATE_EXPR, TREE_TYPE (min), max);
  else if (cmp == 0 || cmp == 1)
    {
      max = min;
      min = fold_unary (NEGATE_EXPR, TREE_TYPE (min), min);
    }
  else
    {
      set_value_range_to_varying (vr);
      return;
    }
  set_and_canonicalize_value_range (vr, VR_RANGE, min, max, NULL);
}


/* Return value range information for VAR.

   If we have no values ranges recorded (ie, VRP is not running), then
   return NULL.  Otherwise create an empty range if none existed for VAR.  */

static value_range_t *
get_value_range (const_tree var)
{
  static const struct value_range_d vr_const_varying
    = { VR_VARYING, NULL_TREE, NULL_TREE, NULL };
  value_range_t *vr;
  tree sym;
  unsigned ver = SSA_NAME_VERSION (var);

  /* If we have no recorded ranges, then return NULL.  */
  if (! vr_value)
    return NULL;

  /* If we query the range for a new SSA name return an unmodifiable VARYING.
     We should get here at most from the substitute-and-fold stage which
     will never try to change values.  */
  if (ver >= num_vr_values)
    return CONST_CAST (value_range_t *, &vr_const_varying);

  vr = vr_value[ver];
  if (vr)
    return vr;

  /* After propagation finished do not allocate new value-ranges.  */
  if (values_propagated)
    return CONST_CAST (value_range_t *, &vr_const_varying);

  /* Create a default value range.  */
  vr_value[ver] = vr = XCNEW (value_range_t);

  /* Defer allocating the equivalence set.  */
  vr->equiv = NULL;

  /* If VAR is a default definition of a parameter, the variable can
     take any value in VAR's type.  */
  if (SSA_NAME_IS_DEFAULT_DEF (var))
    {
      sym = SSA_NAME_VAR (var);
      if (TREE_CODE (sym) == PARM_DECL)
	{
	  /* Try to use the "nonnull" attribute to create ~[0, 0]
	     anti-ranges for pointers.  Note that this is only valid with
	     default definitions of PARM_DECLs.  */
	  if (POINTER_TYPE_P (TREE_TYPE (sym))
	      && nonnull_arg_p (sym))
	    set_value_range_to_nonnull (vr, TREE_TYPE (sym));
	  else
	    set_value_range_to_varying (vr);
	}
      else if (TREE_CODE (sym) == RESULT_DECL
	       && DECL_BY_REFERENCE (sym))
	set_value_range_to_nonnull (vr, TREE_TYPE (sym));
    }

  return vr;
}

/* Return true, if VAL1 and VAL2 are equal values for VRP purposes.  */

static inline bool
vrp_operand_equal_p (const_tree val1, const_tree val2)
{
  if (val1 == val2)
    return true;
  if (!val1 || !val2 || !operand_equal_p (val1, val2, 0))
    return false;
  if (is_overflow_infinity (val1))
    return is_overflow_infinity (val2);
  return true;
}

/* Return true, if the bitmaps B1 and B2 are equal.  */

static inline bool
vrp_bitmap_equal_p (const_bitmap b1, const_bitmap b2)
{
  return (b1 == b2
	  || ((!b1 || bitmap_empty_p (b1))
	      && (!b2 || bitmap_empty_p (b2)))
	  || (b1 && b2
	      && bitmap_equal_p (b1, b2)));
}

/* Update the value range and equivalence set for variable VAR to
   NEW_VR.  Return true if NEW_VR is different from VAR's previous
   value.

   NOTE: This function assumes that NEW_VR is a temporary value range
   object created for the sole purpose of updating VAR's range.  The
   storage used by the equivalence set from NEW_VR will be freed by
   this function.  Do not call update_value_range when NEW_VR
   is the range object associated with another SSA name.  */

static inline bool
update_value_range (const_tree var, value_range_t *new_vr)
{
  value_range_t *old_vr;
  bool is_new;

  /* Update the value range, if necessary.  */
  old_vr = get_value_range (var);
  is_new = old_vr->type != new_vr->type
	   || !vrp_operand_equal_p (old_vr->min, new_vr->min)
	   || !vrp_operand_equal_p (old_vr->max, new_vr->max)
	   || !vrp_bitmap_equal_p (old_vr->equiv, new_vr->equiv);

  if (is_new)
    {
      /* Do not allow transitions up the lattice.  The following
         is slightly more awkward than just new_vr->type < old_vr->type
	 because VR_RANGE and VR_ANTI_RANGE need to be considered
	 the same.  We may not have is_new when transitioning to
	 UNDEFINED or from VARYING.  */
      if (new_vr->type == VR_UNDEFINED
	  || old_vr->type == VR_VARYING)
	set_value_range_to_varying (old_vr);
      else
	set_value_range (old_vr, new_vr->type, new_vr->min, new_vr->max,
			 new_vr->equiv);
    }

  BITMAP_FREE (new_vr->equiv);

  return is_new;
}


/* Add VAR and VAR's equivalence set to EQUIV.  This is the central
   point where equivalence processing can be turned on/off.  */

static void
add_equivalence (bitmap *equiv, const_tree var)
{
  unsigned ver = SSA_NAME_VERSION (var);
  value_range_t *vr = vr_value[ver];

  if (*equiv == NULL)
    *equiv = BITMAP_ALLOC (NULL);
  bitmap_set_bit (*equiv, ver);
  if (vr && vr->equiv)
    bitmap_ior_into (*equiv, vr->equiv);
}


/* Return true if VR is ~[0, 0].  */

static inline bool
range_is_nonnull (value_range_t *vr)
{
  return vr->type == VR_ANTI_RANGE
	 && integer_zerop (vr->min)
	 && integer_zerop (vr->max);
}


/* Return true if VR is [0, 0].  */

static inline bool
range_is_null (value_range_t *vr)
{
  return vr->type == VR_RANGE
	 && integer_zerop (vr->min)
	 && integer_zerop (vr->max);
}

/* Return true if max and min of VR are INTEGER_CST.  It's not necessary
   a singleton.  */

static inline bool
range_int_cst_p (value_range_t *vr)
{
  return (vr->type == VR_RANGE
	  && TREE_CODE (vr->max) == INTEGER_CST
	  && TREE_CODE (vr->min) == INTEGER_CST);
}

/* Return true if VR is a INTEGER_CST singleton.  */

static inline bool
range_int_cst_singleton_p (value_range_t *vr)
{
  return (range_int_cst_p (vr)
	  && !TREE_OVERFLOW (vr->min)
	  && !TREE_OVERFLOW (vr->max)
	  && tree_int_cst_equal (vr->min, vr->max));
}

/* Return true if value range VR involves at least one symbol.  */

static inline bool
symbolic_range_p (value_range_t *vr)
{
  return (!is_gimple_min_invariant (vr->min)
          || !is_gimple_min_invariant (vr->max));
}

/* Return true if value range VR uses an overflow infinity.  */

static inline bool
overflow_infinity_range_p (value_range_t *vr)
{
  return (vr->type == VR_RANGE
	  && (is_overflow_infinity (vr->min)
	      || is_overflow_infinity (vr->max)));
}

/* Return false if we can not make a valid comparison based on VR;
   this will be the case if it uses an overflow infinity and overflow
   is not undefined (i.e., -fno-strict-overflow is in effect).
   Otherwise return true, and set *STRICT_OVERFLOW_P to true if VR
   uses an overflow infinity.  */

static bool
usable_range_p (value_range_t *vr, bool *strict_overflow_p)
{
  gcc_assert (vr->type == VR_RANGE);
  if (is_overflow_infinity (vr->min))
    {
      *strict_overflow_p = true;
      if (!TYPE_OVERFLOW_UNDEFINED (TREE_TYPE (vr->min)))
	return false;
    }
  if (is_overflow_infinity (vr->max))
    {
      *strict_overflow_p = true;
      if (!TYPE_OVERFLOW_UNDEFINED (TREE_TYPE (vr->max)))
	return false;
    }
  return true;
}


/* Return true if the result of assignment STMT is know to be non-negative.
   If the return value is based on the assumption that signed overflow is
   undefined, set *STRICT_OVERFLOW_P to true; otherwise, don't change
   *STRICT_OVERFLOW_P.*/

static bool
gimple_assign_nonnegative_warnv_p (GIMPLE_type stmt, bool *strict_overflow_p)
{
  enum tree_code code = gimple_assign_rhs_code (stmt);
  switch (get_gimple_rhs_class (code))
    {
    case GIMPLE_UNARY_RHS:
      return tree_unary_nonnegative_warnv_p (gimple_assign_rhs_code (stmt),
					     gimple_expr_type (stmt),
					     gimple_assign_rhs1 (stmt),
					     strict_overflow_p);
    case GIMPLE_BINARY_RHS:
      return tree_binary_nonnegative_warnv_p (gimple_assign_rhs_code (stmt),
					      gimple_expr_type (stmt),
					      gimple_assign_rhs1 (stmt),
					      gimple_assign_rhs2 (stmt),
					      strict_overflow_p);
    case GIMPLE_TERNARY_RHS:
      return false;
    case GIMPLE_SINGLE_RHS:
      return tree_single_nonnegative_warnv_p (gimple_assign_rhs1 (stmt),
					      strict_overflow_p);
    case GIMPLE_INVALID_RHS:
      gcc_unreachable ();
    default:
      gcc_unreachable ();
    }
}

/* Return true if return value of call STMT is know to be non-negative.
   If the return value is based on the assumption that signed overflow is
   undefined, set *STRICT_OVERFLOW_P to true; otherwise, don't change
   *STRICT_OVERFLOW_P.*/

static bool
gimple_call_nonnegative_warnv_p (GIMPLE_type stmt, bool *strict_overflow_p)
{
  tree arg0 = gimple_call_num_args (stmt) > 0 ?
    gimple_call_arg (stmt, 0) : NULL_TREE;
  tree arg1 = gimple_call_num_args (stmt) > 1 ?
    gimple_call_arg (stmt, 1) : NULL_TREE;

  return tree_call_nonnegative_warnv_p (gimple_expr_type (stmt),
					gimple_call_fndecl (stmt),
					arg0,
					arg1,
					strict_overflow_p);
}

/* Return true if STMT is know to to compute a non-negative value.
   If the return value is based on the assumption that signed overflow is
   undefined, set *STRICT_OVERFLOW_P to true; otherwise, don't change
   *STRICT_OVERFLOW_P.*/

static bool
gimple_stmt_nonnegative_warnv_p (GIMPLE_type stmt, bool *strict_overflow_p)
{
  switch (gimple_code (stmt))
    {
    case GIMPLE_ASSIGN:
      return gimple_assign_nonnegative_warnv_p (stmt, strict_overflow_p);
    case GIMPLE_CALL:
      return gimple_call_nonnegative_warnv_p (stmt, strict_overflow_p);
    default:
      gcc_unreachable ();
    }
}

/* Return true if the result of assignment STMT is know to be non-zero.
   If the return value is based on the assumption that signed overflow is
   undefined, set *STRICT_OVERFLOW_P to true; otherwise, don't change
   *STRICT_OVERFLOW_P.*/

static bool
gimple_assign_nonzero_warnv_p (GIMPLE_type stmt, bool *strict_overflow_p)
{
  enum tree_code code = gimple_assign_rhs_code (stmt);
  switch (get_gimple_rhs_class (code))
    {
    case GIMPLE_UNARY_RHS:
      return tree_unary_nonzero_warnv_p (gimple_assign_rhs_code (stmt),
					 gimple_expr_type (stmt),
					 gimple_assign_rhs1 (stmt),
					 strict_overflow_p);
    case GIMPLE_BINARY_RHS:
      return tree_binary_nonzero_warnv_p (gimple_assign_rhs_code (stmt),
					  gimple_expr_type (stmt),
					  gimple_assign_rhs1 (stmt),
					  gimple_assign_rhs2 (stmt),
					  strict_overflow_p);
    case GIMPLE_TERNARY_RHS:
      return false;
    case GIMPLE_SINGLE_RHS:
      return tree_single_nonzero_warnv_p (gimple_assign_rhs1 (stmt),
					  strict_overflow_p);
    case GIMPLE_INVALID_RHS:
      gcc_unreachable ();
    default:
      gcc_unreachable ();
    }
}

/* Return true if STMT is know to to compute a non-zero value.
   If the return value is based on the assumption that signed overflow is
   undefined, set *STRICT_OVERFLOW_P to true; otherwise, don't change
   *STRICT_OVERFLOW_P.*/

static bool
gimple_stmt_nonzero_warnv_p (GIMPLE_type stmt, bool *strict_overflow_p)
{
  switch (gimple_code (stmt))
    {
    case GIMPLE_ASSIGN:
      return gimple_assign_nonzero_warnv_p (stmt, strict_overflow_p);
    case GIMPLE_CALL:
      return gimple_alloca_call_p (stmt);
    default:
      gcc_unreachable ();
    }
}

/* Like tree_expr_nonzero_warnv_p, but this function uses value ranges
   obtained so far.  */

static bool
vrp_stmt_computes_nonzero (GIMPLE_type stmt, bool *strict_overflow_p)
{
  if (gimple_stmt_nonzero_warnv_p (stmt, strict_overflow_p))
    return true;

  /* If we have an expression of the form &X->a, then the expression
     is nonnull if X is nonnull.  */
  if (is_gimple_assign (stmt)
      && gimple_assign_rhs_code (stmt) == ADDR_EXPR)
    {
      tree expr = gimple_assign_rhs1 (stmt);
      tree base = get_base_address (TREE_OPERAND (expr, 0));

      if (base != NULL_TREE
	  && TREE_CODE (base) == MEM_REF
	  && TREE_CODE (TREE_OPERAND (base, 0)) == SSA_NAME)
	{
	  value_range_t *vr = get_value_range (TREE_OPERAND (base, 0));
	  if (range_is_nonnull (vr))
	    return true;
	}
    }

  return false;
}

/* Returns true if EXPR is a valid value (as expected by compare_values) --
   a GIMPLE_type invariant, or SSA_NAME +- CST.  */

static bool
valid_value_p (tree expr)
{
  if (TREE_CODE (expr) == SSA_NAME)
    return true;

  if (TREE_CODE (expr) == PLUS_EXPR
      || TREE_CODE (expr) == MINUS_EXPR)
    return (TREE_CODE (TREE_OPERAND (expr, 0)) == SSA_NAME
	    && TREE_CODE (TREE_OPERAND (expr, 1)) == INTEGER_CST);

  return is_gimple_min_invariant (expr);
}

/* Return
   1 if VAL < VAL2
   0 if !(VAL < VAL2)
   -2 if those are incomparable.  */
static inline int
operand_less_p (tree val, tree val2)
{
  /* LT is folded faster than GE and others.  Inline the common case.  */
  if (TREE_CODE (val) == INTEGER_CST && TREE_CODE (val2) == INTEGER_CST)
    {
      if (TYPE_UNSIGNED (TREE_TYPE (val)))
	return INT_CST_LT_UNSIGNED (val, val2);
      else
	{
	  if (INT_CST_LT (val, val2))
	    return 1;
	}
    }
  else
    {
      tree tcmp;

      fold_defer_overflow_warnings ();

      tcmp = fold_binary_to_constant (LT_EXPR, boolean_type_node, val, val2);

      fold_undefer_and_ignore_overflow_warnings ();

      if (!tcmp
	  || TREE_CODE (tcmp) != INTEGER_CST)
	return -2;

      if (!integer_zerop (tcmp))
	return 1;
    }

  /* val >= val2, not considering overflow infinity.  */
  if (is_negative_overflow_infinity (val))
    return is_negative_overflow_infinity (val2) ? 0 : 1;
  else if (is_positive_overflow_infinity (val2))
    return is_positive_overflow_infinity (val) ? 0 : 1;

  return 0;
}

/* Compare two values VAL1 and VAL2.  Return

   	-2 if VAL1 and VAL2 cannot be compared at compile-time,
   	-1 if VAL1 < VAL2,
   	 0 if VAL1 == VAL2,
	+1 if VAL1 > VAL2, and
	+2 if VAL1 != VAL2

   This is similar to tree_int_cst_compare but supports pointer values
   and values that cannot be compared at compile time.

   If STRICT_OVERFLOW_P is not NULL, then set *STRICT_OVERFLOW_P to
   true if the return value is only valid if we assume that signed
   overflow is undefined.  */

static int
compare_values_warnv (tree val1, tree val2, bool *strict_overflow_p)
{
  if (val1 == val2)
    return 0;

  /* Below we rely on the fact that VAL1 and VAL2 are both pointers or
     both integers.  */
  gcc_assert (POINTER_TYPE_P (TREE_TYPE (val1))
	      == POINTER_TYPE_P (TREE_TYPE (val2)));
  /* Convert the two values into the same type.  This is needed because
     sizetype causes sign extension even for unsigned types.  */
  val2 = fold_convert (TREE_TYPE (val1), val2);
  STRIP_USELESS_TYPE_CONVERSION (val2);

  if ((TREE_CODE (val1) == SSA_NAME
       || TREE_CODE (val1) == PLUS_EXPR
       || TREE_CODE (val1) == MINUS_EXPR)
      && (TREE_CODE (val2) == SSA_NAME
	  || TREE_CODE (val2) == PLUS_EXPR
	  || TREE_CODE (val2) == MINUS_EXPR))
    {
      tree n1, c1, n2, c2;
      enum tree_code code1, code2;

      /* If VAL1 and VAL2 are of the form 'NAME [+-] CST' or 'NAME',
	 return -1 or +1 accordingly.  If VAL1 and VAL2 don't use the
	 same name, return -2.  */
      if (TREE_CODE (val1) == SSA_NAME)
	{
	  code1 = SSA_NAME;
	  n1 = val1;
	  c1 = NULL_TREE;
	}
      else
	{
	  code1 = TREE_CODE (val1);
	  n1 = TREE_OPERAND (val1, 0);
	  c1 = TREE_OPERAND (val1, 1);
	  if (tree_int_cst_sgn (c1) == -1)
	    {
	      if (is_negative_overflow_infinity (c1))
		return -2;
	      c1 = fold_unary_to_constant (NEGATE_EXPR, TREE_TYPE (c1), c1);
	      if (!c1)
		return -2;
	      code1 = code1 == MINUS_EXPR ? PLUS_EXPR : MINUS_EXPR;
	    }
	}

      if (TREE_CODE (val2) == SSA_NAME)
	{
	  code2 = SSA_NAME;
	  n2 = val2;
	  c2 = NULL_TREE;
	}
      else
	{
	  code2 = TREE_CODE (val2);
	  n2 = TREE_OPERAND (val2, 0);
	  c2 = TREE_OPERAND (val2, 1);
	  if (tree_int_cst_sgn (c2) == -1)
	    {
	      if (is_negative_overflow_infinity (c2))
		return -2;
	      c2 = fold_unary_to_constant (NEGATE_EXPR, TREE_TYPE (c2), c2);
	      if (!c2)
		return -2;
	      code2 = code2 == MINUS_EXPR ? PLUS_EXPR : MINUS_EXPR;
	    }
	}

      /* Both values must use the same name.  */
      if (n1 != n2)
	return -2;

      if (code1 == SSA_NAME
	  && code2 == SSA_NAME)
	/* NAME == NAME  */
	return 0;

      /* If overflow is defined we cannot simplify more.  */
      if (!TYPE_OVERFLOW_UNDEFINED (TREE_TYPE (val1)))
	return -2;

      if (strict_overflow_p != NULL
	  && (code1 == SSA_NAME || !TREE_NO_WARNING (val1))
	  && (code2 == SSA_NAME || !TREE_NO_WARNING (val2)))
	*strict_overflow_p = true;

      if (code1 == SSA_NAME)
	{
	  if (code2 == PLUS_EXPR)
	    /* NAME < NAME + CST  */
	    return -1;
	  else if (code2 == MINUS_EXPR)
	    /* NAME > NAME - CST  */
	    return 1;
	}
      else if (code1 == PLUS_EXPR)
	{
	  if (code2 == SSA_NAME)
	    /* NAME + CST > NAME  */
	    return 1;
	  else if (code2 == PLUS_EXPR)
	    /* NAME + CST1 > NAME + CST2, if CST1 > CST2  */
	    return compare_values_warnv (c1, c2, strict_overflow_p);
	  else if (code2 == MINUS_EXPR)
	    /* NAME + CST1 > NAME - CST2  */
	    return 1;
	}
      else if (code1 == MINUS_EXPR)
	{
	  if (code2 == SSA_NAME)
	    /* NAME - CST < NAME  */
	    return -1;
	  else if (code2 == PLUS_EXPR)
	    /* NAME - CST1 < NAME + CST2  */
	    return -1;
	  else if (code2 == MINUS_EXPR)
	    /* NAME - CST1 > NAME - CST2, if CST1 < CST2.  Notice that
	       C1 and C2 are swapped in the call to compare_values.  */
	    return compare_values_warnv (c2, c1, strict_overflow_p);
	}

      gcc_unreachable ();
    }

  /* We cannot compare non-constants.  */
  if (!is_gimple_min_invariant (val1) || !is_gimple_min_invariant (val2))
    return -2;

  if (!POINTER_TYPE_P (TREE_TYPE (val1)))
    {
      /* We cannot compare overflowed values, except for overflow
	 infinities.  */
      if (TREE_OVERFLOW (val1) || TREE_OVERFLOW (val2))
	{
	  if (strict_overflow_p != NULL)
	    *strict_overflow_p = true;
	  if (is_negative_overflow_infinity (val1))
	    return is_negative_overflow_infinity (val2) ? 0 : -1;
	  else if (is_negative_overflow_infinity (val2))
	    return 1;
	  else if (is_positive_overflow_infinity (val1))
	    return is_positive_overflow_infinity (val2) ? 0 : 1;
	  else if (is_positive_overflow_infinity (val2))
	    return -1;
	  return -2;
	}

      return tree_int_cst_compare (val1, val2);
    }
  else
    {
      tree t;

      /* First see if VAL1 and VAL2 are not the same.  */
      if (val1 == val2 || operand_equal_p (val1, val2, 0))
	return 0;

      /* If VAL1 is a lower address than VAL2, return -1.  */
      if (operand_less_p (val1, val2) == 1)
	return -1;

      /* If VAL1 is a higher address than VAL2, return +1.  */
      if (operand_less_p (val2, val1) == 1)
	return 1;

      /* If VAL1 is different than VAL2, return +2.
	 For integer constants we either have already returned -1 or 1
	 or they are equivalent.  We still might succeed in proving
	 something about non-trivial operands.  */
      if (TREE_CODE (val1) != INTEGER_CST
	  || TREE_CODE (val2) != INTEGER_CST)
	{
          t = fold_binary_to_constant (NE_EXPR, boolean_type_node, val1, val2);
	  if (t && integer_onep (t))
	    return 2;
	}

      return -2;
    }
}

/* Compare values like compare_values_warnv, but treat comparisons of
   nonconstants which rely on undefined overflow as incomparable.  */

static int
compare_values (tree val1, tree val2)
{
  bool sop;
  int ret;

  sop = false;
  ret = compare_values_warnv (val1, val2, &sop);
  if (sop
      && (!is_gimple_min_invariant (val1) || !is_gimple_min_invariant (val2)))
    ret = -2;
  return ret;
}


/* Return 1 if VAL is inside value range MIN <= VAL <= MAX,
          0 if VAL is not inside [MIN, MAX],
	 -2 if we cannot tell either way.

   Benchmark compile/20001226-1.c compilation time after changing this
   function.  */

static inline int
value_inside_range (tree val, tree min, tree max)
{
  int cmp1, cmp2;

  cmp1 = operand_less_p (val, min);
  if (cmp1 == -2)
    return -2;
  if (cmp1 == 1)
    return 0;

  cmp2 = operand_less_p (max, val);
  if (cmp2 == -2)
    return -2;

  return !cmp2;
}


/* Return true if value ranges VR0 and VR1 have a non-empty
   intersection.

   Benchmark compile/20001226-1.c compilation time after changing this
   function.
   */

static inline bool
value_ranges_intersect_p (value_range_t *vr0, value_range_t *vr1)
{
  /* The value ranges do not intersect if the maximum of the first range is
     less than the minimum of the second range or vice versa.
     When those relations are unknown, we can't do any better.  */
  if (operand_less_p (vr0->max, vr1->min) != 0)
    return false;
  if (operand_less_p (vr1->max, vr0->min) != 0)
    return false;
  return true;
}


/* Return 1 if [MIN, MAX] includes the value zero, 0 if it does not
   include the value zero, -2 if we cannot tell.  */

static inline int
range_includes_zero_p (tree min, tree max)
{
  tree zero = build_int_cst (TREE_TYPE (min), 0);
  return value_inside_range (zero, min, max);
}

/* Return true if *VR is know to only contain nonnegative values.  */

static inline bool
value_range_nonnegative_p (value_range_t *vr)
{
  /* Testing for VR_ANTI_RANGE is not useful here as any anti-range
     which would return a useful value should be encoded as a 
     VR_RANGE.  */
  if (vr->type == VR_RANGE)
    {
      int result = compare_values (vr->min, integer_zero_node);
      return (result == 0 || result == 1);
    }

  return false;
}

/* Return true if T, an SSA_NAME, is known to be nonnegative.  Return
   false otherwise or if no value range information is available.  */

bool
ssa_name_nonnegative_p (const_tree t)
{
  value_range_t *vr = get_value_range (t);

  if (INTEGRAL_TYPE_P (t)
      && TYPE_UNSIGNED (t))
    return true;

  if (!vr)
    return false;

  return value_range_nonnegative_p (vr);
}

/* If *VR has a value rante that is a single constant value return that,
   otherwise return NULL_TREE.  */

static tree
value_range_constant_singleton (value_range_t *vr)
{
  if (vr->type == VR_RANGE
      && operand_equal_p (vr->min, vr->max, 0)
      && is_gimple_min_invariant (vr->min))
    return vr->min;

  return NULL_TREE;
}

/* If OP has a value range with a single constant value return that,
   otherwise return NULL_TREE.  This returns OP itself if OP is a
   constant.  */

static tree
op_with_constant_singleton_value_range (tree op)
{
  if (is_gimple_min_invariant (op))
    return op;

  if (TREE_CODE (op) != SSA_NAME)
    return NULL_TREE;

  return value_range_constant_singleton (get_value_range (op));
}

/* Return true if op is in a boolean [0, 1] value-range.  */

static bool
op_with_boolean_value_range_p (tree op)
{
  value_range_t *vr;

  if (TYPE_PRECISION (TREE_TYPE (op)) == 1)
    return true;

  if (integer_zerop (op)
      || integer_onep (op))
    return true;

  if (TREE_CODE (op) != SSA_NAME)
    return false;

  vr = get_value_range (op);
  return (vr->type == VR_RANGE
	  && integer_zerop (vr->min)
	  && integer_onep (vr->max));
}

/* Extract value range information from an ASSERT_EXPR EXPR and store
   it in *VR_P.  */

static void
extract_range_from_assert (value_range_t *vr_p, tree expr)
{
  tree var, cond, limit, min, max, type;
  value_range_t *limit_vr;
  enum tree_code cond_code;

  var = ASSERT_EXPR_VAR (expr);
  cond = ASSERT_EXPR_COND (expr);

  gcc_assert (COMPARISON_CLASS_P (cond));

  /* Find VAR in the ASSERT_EXPR conditional.  */
  if (var == TREE_OPERAND (cond, 0)
      || TREE_CODE (TREE_OPERAND (cond, 0)) == PLUS_EXPR
      || TREE_CODE (TREE_OPERAND (cond, 0)) == NOP_EXPR)
    {
      /* If the predicate is of the form VAR COMP LIMIT, then we just
	 take LIMIT from the RHS and use the same comparison code.  */
      cond_code = TREE_CODE (cond);
      limit = TREE_OPERAND (cond, 1);
      cond = TREE_OPERAND (cond, 0);
    }
  else
    {
      /* If the predicate is of the form LIMIT COMP VAR, then we need
	 to flip around the comparison code to create the proper range
	 for VAR.  */
      cond_code = swap_tree_comparison (TREE_CODE (cond));
      limit = TREE_OPERAND (cond, 0);
      cond = TREE_OPERAND (cond, 1);
    }

  limit = avoid_overflow_infinity (limit);

  type = TREE_TYPE (var);
  gcc_assert (limit != var);

  /* For pointer arithmetic, we only keep track of pointer equality
     and inequality.  */
  if (POINTER_TYPE_P (type) && cond_code != NE_EXPR && cond_code != EQ_EXPR)
    {
      set_value_range_to_varying (vr_p);
      return;
    }

  /* If LIMIT is another SSA name and LIMIT has a range of its own,
     try to use LIMIT's range to avoid creating symbolic ranges
     unnecessarily. */
  limit_vr = (TREE_CODE (limit) == SSA_NAME) ? get_value_range (limit) : NULL;

  /* LIMIT's range is only interesting if it has any useful information.  */
  if (limit_vr
      && (limit_vr->type == VR_UNDEFINED
	  || limit_vr->type == VR_VARYING
	  || symbolic_range_p (limit_vr)))
    limit_vr = NULL;

  /* Initially, the new range has the same set of equivalences of
     VAR's range.  This will be revised before returning the final
     value.  Since assertions may be chained via mutually exclusive
     predicates, we will need to trim the set of equivalences before
     we are done.  */
  gcc_assert (vr_p->equiv == NULL);
  add_equivalence (&vr_p->equiv, var);

  /* Extract a new range based on the asserted comparison for VAR and
     LIMIT's value range.  Notice that if LIMIT has an anti-range, we
     will only use it for equality comparisons (EQ_EXPR).  For any
     other kind of assertion, we cannot derive a range from LIMIT's
     anti-range that can be used to describe the new range.  For
     instance, ASSERT_EXPR <x_2, x_2 <= b_4>.  If b_4 is ~[2, 10],
     then b_4 takes on the ranges [-INF, 1] and [11, +INF].  There is
     no single range for x_2 that could describe LE_EXPR, so we might
     as well build the range [b_4, +INF] for it.
     One special case we handle is extracting a range from a
     range test encoded as (unsigned)var + CST <= limit.  */
  if (TREE_CODE (cond) == NOP_EXPR
      || TREE_CODE (cond) == PLUS_EXPR)
    {
      if (TREE_CODE (cond) == PLUS_EXPR)
        {
          min = fold_build1 (NEGATE_EXPR, TREE_TYPE (TREE_OPERAND (cond, 1)),
			     TREE_OPERAND (cond, 1));
          max = int_const_binop (PLUS_EXPR, limit, min);
	  cond = TREE_OPERAND (cond, 0);
	}
      else
	{
	  min = build_int_cst (TREE_TYPE (var), 0);
	  max = limit;
	}

      /* Make sure to not set TREE_OVERFLOW on the final type
	 conversion.  We are willingly interpreting large positive
	 unsigned values as negative singed values here.  */
      min = force_fit_type_double (TREE_TYPE (var), tree_to_double_int (min),
				   0, false);
      max = force_fit_type_double (TREE_TYPE (var), tree_to_double_int (max),
				   0, false);

      /* We can transform a max, min range to an anti-range or
         vice-versa.  Use set_and_canonicalize_value_range which does
	 this for us.  */
      if (cond_code == LE_EXPR)
        set_and_canonicalize_value_range (vr_p, VR_RANGE,
					  min, max, vr_p->equiv);
      else if (cond_code == GT_EXPR)
        set_and_canonicalize_value_range (vr_p, VR_ANTI_RANGE,
					  min, max, vr_p->equiv);
      else
	gcc_unreachable ();
    }
  else if (cond_code == EQ_EXPR)
    {
      enum value_range_type range_type;

      if (limit_vr)
	{
	  range_type = limit_vr->type;
	  min = limit_vr->min;
	  max = limit_vr->max;
	}
      else
	{
	  range_type = VR_RANGE;
	  min = limit;
	  max = limit;
	}

      set_value_range (vr_p, range_type, min, max, vr_p->equiv);

      /* When asserting the equality VAR == LIMIT and LIMIT is another
	 SSA name, the new range will also inherit the equivalence set
	 from LIMIT.  */
      if (TREE_CODE (limit) == SSA_NAME)
	add_equivalence (&vr_p->equiv, limit);
    }
  else if (cond_code == NE_EXPR)
    {
      /* As described above, when LIMIT's range is an anti-range and
	 this assertion is an inequality (NE_EXPR), then we cannot
	 derive anything from the anti-range.  For instance, if
	 LIMIT's range was ~[0, 0], the assertion 'VAR != LIMIT' does
	 not imply that VAR's range is [0, 0].  So, in the case of
	 anti-ranges, we just assert the inequality using LIMIT and
	 not its anti-range.

	 If LIMIT_VR is a range, we can only use it to build a new
	 anti-range if LIMIT_VR is a single-valued range.  For
	 instance, if LIMIT_VR is [0, 1], the predicate
	 VAR != [0, 1] does not mean that VAR's range is ~[0, 1].
	 Rather, it means that for value 0 VAR should be ~[0, 0]
	 and for value 1, VAR should be ~[1, 1].  We cannot
	 represent these ranges.

	 The only situation in which we can build a valid
	 anti-range is when LIMIT_VR is a single-valued range
	 (i.e., LIMIT_VR->MIN == LIMIT_VR->MAX).  In that case,
	 build the anti-range ~[LIMIT_VR->MIN, LIMIT_VR->MAX].  */
      if (limit_vr
	  && limit_vr->type == VR_RANGE
	  && compare_values (limit_vr->min, limit_vr->max) == 0)
	{
	  min = limit_vr->min;
	  max = limit_vr->max;
	}
      else
	{
	  /* In any other case, we cannot use LIMIT's range to build a
	     valid anti-range.  */
	  min = max = limit;
	}

      /* If MIN and MAX cover the whole range for their type, then
	 just use the original LIMIT.  */
      if (INTEGRAL_TYPE_P (type)
	  && vrp_val_is_min (min)
	  && vrp_val_is_max (max))
	min = max = limit;

      set_and_canonicalize_value_range (vr_p, VR_ANTI_RANGE,
					min, max, vr_p->equiv);
    }
  else if (cond_code == LE_EXPR || cond_code == LT_EXPR)
    {
      min = TYPE_MIN_VALUE (type);

      if (limit_vr == NULL || limit_vr->type == VR_ANTI_RANGE)
	max = limit;
      else
	{
	  /* If LIMIT_VR is of the form [N1, N2], we need to build the
	     range [MIN, N2] for LE_EXPR and [MIN, N2 - 1] for
	     LT_EXPR.  */
	  max = limit_vr->max;
	}

      /* If the maximum value forces us to be out of bounds, simply punt.
	 It would be pointless to try and do anything more since this
	 all should be optimized away above us.  */
      if ((cond_code == LT_EXPR
	   && compare_values (max, min) == 0)
	  || (CONSTANT_CLASS_P (max) && TREE_OVERFLOW (max)))
	set_value_range_to_varying (vr_p);
      else
	{
	  /* For LT_EXPR, we create the range [MIN, MAX - 1].  */
	  if (cond_code == LT_EXPR)
	    {
	      if (TYPE_PRECISION (TREE_TYPE (max)) == 1
		  && !TYPE_UNSIGNED (TREE_TYPE (max)))
		max = fold_build2 (PLUS_EXPR, TREE_TYPE (max), max,
				   build_int_cst (TREE_TYPE (max), -1));
	      else
		max = fold_build2 (MINUS_EXPR, TREE_TYPE (max), max,
				   build_int_cst (TREE_TYPE (max), 1));
	      if (EXPR_P (max))
		TREE_NO_WARNING (max) = 1;
	    }

	  set_value_range (vr_p, VR_RANGE, min, max, vr_p->equiv);
	}
    }
  else if (cond_code == GE_EXPR || cond_code == GT_EXPR)
    {
      max = TYPE_MAX_VALUE (type);

      if (limit_vr == NULL || limit_vr->type == VR_ANTI_RANGE)
	min = limit;
      else
	{
	  /* If LIMIT_VR is of the form [N1, N2], we need to build the
	     range [N1, MAX] for GE_EXPR and [N1 + 1, MAX] for
	     GT_EXPR.  */
	  min = limit_vr->min;
	}

      /* If the minimum value forces us to be out of bounds, simply punt.
	 It would be pointless to try and do anything more since this
	 all should be optimized away above us.  */
      if ((cond_code == GT_EXPR
	   && compare_values (min, max) == 0)
	  || (CONSTANT_CLASS_P (min) && TREE_OVERFLOW (min)))
	set_value_range_to_varying (vr_p);
      else
	{
	  /* For GT_EXPR, we create the range [MIN + 1, MAX].  */
	  if (cond_code == GT_EXPR)
	    {
	      if (TYPE_PRECISION (TREE_TYPE (min)) == 1
		  && !TYPE_UNSIGNED (TREE_TYPE (min)))
		min = fold_build2 (MINUS_EXPR, TREE_TYPE (min), min,
				   build_int_cst (TREE_TYPE (min), -1));
	      else
		min = fold_build2 (PLUS_EXPR, TREE_TYPE (min), min,
				   build_int_cst (TREE_TYPE (min), 1));
	      if (EXPR_P (min))
		TREE_NO_WARNING (min) = 1;
	    }

	  set_value_range (vr_p, VR_RANGE, min, max, vr_p->equiv);
	}
    }
  else
    gcc_unreachable ();

  /* Finally intersect the new range with what we already know about var.  */
  vrp_intersect_ranges (vr_p, get_value_range (var));
}


/* Extract range information from SSA name VAR and store it in VR.  If
   VAR has an interesting range, use it.  Otherwise, create the
   range [VAR, VAR] and return it.  This is useful in situations where
   we may have conditionals testing values of VARYING names.  For
   instance,

   	x_3 = y_5;
	if (x_3 > y_5)
	  ...

    Even if y_5 is deemed VARYING, we can determine that x_3 > y_5 is
    always false.  */

static void
extract_range_from_ssa_name (value_range_t *vr, tree var)
{
  value_range_t *var_vr = get_value_range (var);

  if (var_vr->type != VR_UNDEFINED && var_vr->type != VR_VARYING)
    copy_value_range (vr, var_vr);
  else
    set_value_range (vr, VR_RANGE, var, var, NULL);

  add_equivalence (&vr->equiv, var);
}


/* Wrapper around int_const_binop.  If the operation overflows and we
   are not using wrapping arithmetic, then adjust the result to be
   -INF or +INF depending on CODE, VAL1 and VAL2.  This can return
   NULL_TREE if we need to use an overflow infinity representation but
   the type does not support it.  */

static tree
vrp_int_const_binop (enum tree_code code, tree val1, tree val2)
{
  tree res;

  res = int_const_binop (code, val1, val2);

  /* If we are using unsigned arithmetic, operate symbolically
     on -INF and +INF as int_const_binop only handles signed overflow.  */
  if (TYPE_UNSIGNED (TREE_TYPE (val1)))
    {
      int checkz = compare_values (res, val1);
      bool overflow = false;

      /* Ensure that res = val1 [+*] val2 >= val1
         or that res = val1 - val2 <= val1.  */
      if ((code == PLUS_EXPR
	   && !(checkz == 1 || checkz == 0))
          || (code == MINUS_EXPR
	      && !(checkz == 0 || checkz == -1)))
	{
	  overflow = true;
	}
      /* Checking for multiplication overflow is done by dividing the
	 output of the multiplication by the first input of the
	 multiplication.  If the result of that division operation is
	 not equal to the second input of the multiplication, then the
	 multiplication overflowed.  */
      else if (code == MULT_EXPR && !integer_zerop (val1))
	{
	  tree tmp = int_const_binop (TRUNC_DIV_EXPR,
				      res,
				      val1);
	  int check = compare_values (tmp, val2);

	  if (check != 0)
	    overflow = true;
	}

      if (overflow)
	{
	  res = copy_node (res);
	  TREE_OVERFLOW (res) = 1;
	}

    }
  else if (TYPE_OVERFLOW_WRAPS (TREE_TYPE (val1)))
    /* If the singed operation wraps then int_const_binop has done
       everything we want.  */
    ;
  else if ((TREE_OVERFLOW (res)
	    && !TREE_OVERFLOW (val1)
	    && !TREE_OVERFLOW (val2))
	   || is_overflow_infinity (val1)
	   || is_overflow_infinity (val2))
    {
      /* If the operation overflowed but neither VAL1 nor VAL2 are
	 overflown, return -INF or +INF depending on the operation
	 and the combination of signs of the operands.  */
      int sgn1 = tree_int_cst_sgn (val1);
      int sgn2 = tree_int_cst_sgn (val2);

      if (needs_overflow_infinity (TREE_TYPE (res))
	  && !supports_overflow_infinity (TREE_TYPE (res)))
	return NULL_TREE;

      /* We have to punt on adding infinities of different signs,
	 since we can't tell what the sign of the result should be.
	 Likewise for subtracting infinities of the same sign.  */
      if (((code == PLUS_EXPR && sgn1 != sgn2)
	   || (code == MINUS_EXPR && sgn1 == sgn2))
	  && is_overflow_infinity (val1)
	  && is_overflow_infinity (val2))
	return NULL_TREE;

      /* Don't try to handle division or shifting of infinities.  */
      if ((code == TRUNC_DIV_EXPR
	   || code == FLOOR_DIV_EXPR
	   || code == CEIL_DIV_EXPR
	   || code == EXACT_DIV_EXPR
	   || code == ROUND_DIV_EXPR
	   || code == RSHIFT_EXPR)
	  && (is_overflow_infinity (val1)
	      || is_overflow_infinity (val2)))
	return NULL_TREE;

      /* Notice that we only need to handle the restricted set of
	 operations handled by extract_range_from_binary_expr.
	 Among them, only multiplication, addition and subtraction
	 can yield overflow without overflown operands because we
	 are working with integral types only... except in the
	 case VAL1 = -INF and VAL2 = -1 which overflows to +INF
	 for division too.  */

      /* For multiplication, the sign of the overflow is given
	 by the comparison of the signs of the operands.  */
      if ((code == MULT_EXPR && sgn1 == sgn2)
          /* For addition, the operands must be of the same sign
	     to yield an overflow.  Its sign is therefore that
	     of one of the operands, for example the first.  For
	     infinite operands X + -INF is negative, not positive.  */
	  || (code == PLUS_EXPR
	      && (sgn1 >= 0
		  ? !is_negative_overflow_infinity (val2)
		  : is_positive_overflow_infinity (val2)))
	  /* For subtraction, non-infinite operands must be of
	     different signs to yield an overflow.  Its sign is
	     therefore that of the first operand or the opposite of
	     that of the second operand.  A first operand of 0 counts
	     as positive here, for the corner case 0 - (-INF), which
	     overflows, but must yield +INF.  For infinite operands 0
	     - INF is negative, not positive.  */
	  || (code == MINUS_EXPR
	      && (sgn1 >= 0
		  ? !is_positive_overflow_infinity (val2)
		  : is_negative_overflow_infinity (val2)))
	  /* We only get in here with positive shift count, so the
	     overflow direction is the same as the sign of val1.
	     Actually rshift does not overflow at all, but we only
	     handle the case of shifting overflowed -INF and +INF.  */
	  || (code == RSHIFT_EXPR
	      && sgn1 >= 0)
	  /* For division, the only case is -INF / -1 = +INF.  */
	  || code == TRUNC_DIV_EXPR
	  || code == FLOOR_DIV_EXPR
	  || code == CEIL_DIV_EXPR
	  || code == EXACT_DIV_EXPR
	  || code == ROUND_DIV_EXPR)
	return (needs_overflow_infinity (TREE_TYPE (res))
		? positive_overflow_infinity (TREE_TYPE (res))
		: TYPE_MAX_VALUE (TREE_TYPE (res)));
      else
	return (needs_overflow_infinity (TREE_TYPE (res))
		? negative_overflow_infinity (TREE_TYPE (res))
		: TYPE_MIN_VALUE (TREE_TYPE (res)));
    }

  return res;
}


/* For range VR compute two double_int bitmasks.  In *MAY_BE_NONZERO
   bitmask if some bit is unset, it means for all numbers in the range
   the bit is 0, otherwise it might be 0 or 1.  In *MUST_BE_NONZERO
   bitmask if some bit is set, it means for all numbers in the range
   the bit is 1, otherwise it might be 0 or 1.  */

static bool
zero_nonzero_bits_from_vr (value_range_t *vr,
			   double_int *may_be_nonzero,
			   double_int *must_be_nonzero)
{
  *may_be_nonzero = double_int_minus_one;
  *must_be_nonzero = double_int_zero;
  if (!range_int_cst_p (vr)
      || TREE_OVERFLOW (vr->min)
      || TREE_OVERFLOW (vr->max))
    return false;

  if (range_int_cst_singleton_p (vr))
    {
      *may_be_nonzero = tree_to_double_int (vr->min);
      *must_be_nonzero = *may_be_nonzero;
    }
  else if (tree_int_cst_sgn (vr->min) >= 0
	   || tree_int_cst_sgn (vr->max) < 0)
    {
      double_int dmin = tree_to_double_int (vr->min);
      double_int dmax = tree_to_double_int (vr->max);
      double_int xor_mask = dmin ^ dmax;
      *may_be_nonzero = dmin | dmax;
      *must_be_nonzero = dmin & dmax;
      if (xor_mask.high != 0)
	{
	  unsigned HOST_WIDE_INT mask
	      = ((unsigned HOST_WIDE_INT) 1
		 << floor_log2 (xor_mask.high)) - 1;
	  may_be_nonzero->low = ALL_ONES;
	  may_be_nonzero->high |= mask;
	  must_be_nonzero->low = 0;
	  must_be_nonzero->high &= ~mask;
	}
      else if (xor_mask.low != 0)
	{
	  unsigned HOST_WIDE_INT mask
	      = ((unsigned HOST_WIDE_INT) 1
		 << floor_log2 (xor_mask.low)) - 1;
	  may_be_nonzero->low |= mask;
	  must_be_nonzero->low &= ~mask;
	}
    }

  return true;
}

/* Create two value-ranges in *VR0 and *VR1 from the anti-range *AR
   so that *VR0 U *VR1 == *AR.  Returns true if that is possible,
   false otherwise.  If *AR can be represented with a single range
   *VR1 will be VR_UNDEFINED.  */

static bool
ranges_from_anti_range (value_range_t *ar,
			value_range_t *vr0, value_range_t *vr1)
{
  tree type = TREE_TYPE (ar->min);

  vr0->type = VR_UNDEFINED;
  vr1->type = VR_UNDEFINED;

  if (ar->type != VR_ANTI_RANGE
      || TREE_CODE (ar->min) != INTEGER_CST
      || TREE_CODE (ar->max) != INTEGER_CST
      || !vrp_val_min (type)
      || !vrp_val_max (type))
    return false;

  if (!vrp_val_is_min (ar->min))
    {
      vr0->type = VR_RANGE;
      vr0->min = vrp_val_min (type);
      vr0->max
	= double_int_to_tree (type,
			      tree_to_double_int (ar->min) - double_int_one);
    }
  if (!vrp_val_is_max (ar->max))
    {
      vr1->type = VR_RANGE;
      vr1->min
	= double_int_to_tree (type,
			      tree_to_double_int (ar->max) + double_int_one);
      vr1->max = vrp_val_max (type);
    }
  if (vr0->type == VR_UNDEFINED)
    {
      *vr0 = *vr1;
      vr1->type = VR_UNDEFINED;
    }

  return vr0->type != VR_UNDEFINED;
}

/* Helper to extract a value-range *VR for a multiplicative operation
   *VR0 CODE *VR1.  */

static void
extract_range_from_multiplicative_op_1 (value_range_t *vr,
					enum tree_code code,
					value_range_t *vr0, value_range_t *vr1)
{
  enum value_range_type type;
  tree val[4];
  size_t i;
  tree min, max;
  bool sop;
  int cmp;

  /* Multiplications, divisions and shifts are a bit tricky to handle,
     depending on the mix of signs we have in the two ranges, we
     need to operate on different values to get the minimum and
     maximum values for the new range.  One approach is to figure
     out all the variations of range combinations and do the
     operations.

     However, this involves several calls to compare_values and it
     is pretty convoluted.  It's simpler to do the 4 operations
     (MIN0 OP MIN1, MIN0 OP MAX1, MAX0 OP MIN1 and MAX0 OP MAX0 OP
     MAX1) and then figure the smallest and largest values to form
     the new range.  */
  gcc_assert (code == MULT_EXPR
	      || code == TRUNC_DIV_EXPR
	      || code == FLOOR_DIV_EXPR
	      || code == CEIL_DIV_EXPR
	      || code == EXACT_DIV_EXPR
	      || code == ROUND_DIV_EXPR
	      || code == RSHIFT_EXPR
	      || code == LSHIFT_EXPR);
  gcc_assert ((vr0->type == VR_RANGE
	       || (code == MULT_EXPR && vr0->type == VR_ANTI_RANGE))
	      && vr0->type == vr1->type);

  type = vr0->type;

  /* Compute the 4 cross operations.  */
  sop = false;
  val[0] = vrp_int_const_binop (code, vr0->min, vr1->min);
  if (val[0] == NULL_TREE)
    sop = true;

  if (vr1->max == vr1->min)
    val[1] = NULL_TREE;
  else
    {
      val[1] = vrp_int_const_binop (code, vr0->min, vr1->max);
      if (val[1] == NULL_TREE)
	sop = true;
    }

  if (vr0->max == vr0->min)
    val[2] = NULL_TREE;
  else
    {
      val[2] = vrp_int_const_binop (code, vr0->max, vr1->min);
      if (val[2] == NULL_TREE)
	sop = true;
    }

  if (vr0->min == vr0->max || vr1->min == vr1->max)
    val[3] = NULL_TREE;
  else
    {
      val[3] = vrp_int_const_binop (code, vr0->max, vr1->max);
      if (val[3] == NULL_TREE)
	sop = true;
    }

  if (sop)
    {
      set_value_range_to_varying (vr);
      return;
    }

  /* Set MIN to the minimum of VAL[i] and MAX to the maximum
     of VAL[i].  */
  min = val[0];
  max = val[0];
  for (i = 1; i < 4; i++)
    {
      if (!is_gimple_min_invariant (min)
	  || (TREE_OVERFLOW (min) && !is_overflow_infinity (min))
	  || !is_gimple_min_invariant (max)
	  || (TREE_OVERFLOW (max) && !is_overflow_infinity (max)))
	break;

      if (val[i])
	{
	  if (!is_gimple_min_invariant (val[i])
	      || (TREE_OVERFLOW (val[i])
		  && !is_overflow_infinity (val[i])))
	    {
	      /* If we found an overflowed value, set MIN and MAX
		 to it so that we set the resulting range to
		 VARYING.  */
	      min = max = val[i];
	      break;
	    }

	  if (compare_values (val[i], min) == -1)
	    min = val[i];

	  if (compare_values (val[i], max) == 1)
	    max = val[i];
	}
    }

  /* If either MIN or MAX overflowed, then set the resulting range to
     VARYING.  But we do accept an overflow infinity
     representation.  */
  if (min == NULL_TREE
      || !is_gimple_min_invariant (min)
      || (TREE_OVERFLOW (min) && !is_overflow_infinity (min))
      || max == NULL_TREE
      || !is_gimple_min_invariant (max)
      || (TREE_OVERFLOW (max) && !is_overflow_infinity (max)))
    {
      set_value_range_to_varying (vr);
      return;
    }

  /* We punt if:
     1) [-INF, +INF]
     2) [-INF, +-INF(OVF)]
     3) [+-INF(OVF), +INF]
     4) [+-INF(OVF), +-INF(OVF)]
     We learn nothing when we have INF and INF(OVF) on both sides.
     Note that we do accept [-INF, -INF] and [+INF, +INF] without
     overflow.  */
  if ((vrp_val_is_min (min) || is_overflow_infinity (min))
      && (vrp_val_is_max (max) || is_overflow_infinity (max)))
    {
      set_value_range_to_varying (vr);
      return;
    }

  cmp = compare_values (min, max);
  if (cmp == -2 || cmp == 1)
    {
      /* If the new range has its limits swapped around (MIN > MAX),
	 then the operation caused one of them to wrap around, mark
	 the new range VARYING.  */
      set_value_range_to_varying (vr);
    }
  else
    set_value_range (vr, type, min, max, NULL);
}

/* Some quadruple precision helpers.  */
static int
quad_int_cmp (double_int l0, double_int h0,
	      double_int l1, double_int h1, bool uns)
{
  int c = h0.cmp (h1, uns);
  if (c != 0) return c;
  return l0.ucmp (l1);
}

static void
quad_int_pair_sort (double_int *l0, double_int *h0,
		    double_int *l1, double_int *h1, bool uns)
{
  if (quad_int_cmp (*l0, *h0, *l1, *h1, uns) > 0)
    {
      double_int tmp;
      tmp = *l0; *l0 = *l1; *l1 = tmp;
      tmp = *h0; *h0 = *h1; *h1 = tmp;
    }
}

/* Extract range information from a binary operation CODE based on
   the ranges of each of its operands, *VR0 and *VR1 with resulting
   type EXPR_TYPE.  The resulting range is stored in *VR.  */

static void
extract_range_from_binary_expr_1 (value_range_t *vr,
				  enum tree_code code, tree expr_type,
				  value_range_t *vr0_, value_range_t *vr1_)
{
  value_range_t vr0 = *vr0_, vr1 = *vr1_;
  value_range_t vrtem0 = VR_INITIALIZER, vrtem1 = VR_INITIALIZER;
  enum value_range_type type;
  tree min = NULL_TREE, max = NULL_TREE;
  int cmp;

  if (!INTEGRAL_TYPE_P (expr_type)
      && !POINTER_TYPE_P (expr_type))
    {
      set_value_range_to_varying (vr);
      return;
    }

  /* Not all binary expressions can be applied to ranges in a
     meaningful way.  Handle only arithmetic operations.  */
  if (code != PLUS_EXPR
      && code != MINUS_EXPR
      && code != POINTER_PLUS_EXPR
      && code != MULT_EXPR
      && code != TRUNC_DIV_EXPR
      && code != FLOOR_DIV_EXPR
      && code != CEIL_DIV_EXPR
      && code != EXACT_DIV_EXPR
      && code != ROUND_DIV_EXPR
      && code != TRUNC_MOD_EXPR
      && code != RSHIFT_EXPR
      && code != LSHIFT_EXPR
      && code != MIN_EXPR
      && code != MAX_EXPR
      && code != BIT_AND_EXPR
      && code != BIT_IOR_EXPR
      && code != BIT_XOR_EXPR)
    {
      set_value_range_to_varying (vr);
      return;
    }

  /* If both ranges are UNDEFINED, so is the result.  */
  if (vr0.type == VR_UNDEFINED && vr1.type == VR_UNDEFINED)
    {
      set_value_range_to_undefined (vr);
      return;
    }
  /* If one of the ranges is UNDEFINED drop it to VARYING for the following
     code.  At some point we may want to special-case operations that
     have UNDEFINED result for all or some value-ranges of the not UNDEFINED
     operand.  */
  else if (vr0.type == VR_UNDEFINED)
    set_value_range_to_varying (&vr0);
  else if (vr1.type == VR_UNDEFINED)
    set_value_range_to_varying (&vr1);

  /* Now canonicalize anti-ranges to ranges when they are not symbolic
     and express ~[] op X as ([]' op X) U ([]'' op X).  */
  if (vr0.type == VR_ANTI_RANGE
      && ranges_from_anti_range (&vr0, &vrtem0, &vrtem1))
    {
      extract_range_from_binary_expr_1 (vr, code, expr_type, &vrtem0, vr1_);
      if (vrtem1.type != VR_UNDEFINED)
	{
	  value_range_t vrres = VR_INITIALIZER;
	  extract_range_from_binary_expr_1 (&vrres, code, expr_type,
					    &vrtem1, vr1_);
	  vrp_meet (vr, &vrres);
	}
      return;
    }
  /* Likewise for X op ~[].  */
  if (vr1.type == VR_ANTI_RANGE
      && ranges_from_anti_range (&vr1, &vrtem0, &vrtem1))
    {
      extract_range_from_binary_expr_1 (vr, code, expr_type, vr0_, &vrtem0);
      if (vrtem1.type != VR_UNDEFINED)
	{
	  value_range_t vrres = VR_INITIALIZER;
	  extract_range_from_binary_expr_1 (&vrres, code, expr_type,
					    vr0_, &vrtem1);
	  vrp_meet (vr, &vrres);
	}
      return;
    }

  /* The type of the resulting value range defaults to VR0.TYPE.  */
  type = vr0.type;

  /* Refuse to operate on VARYING ranges, ranges of different kinds
     and symbolic ranges.  As an exception, we allow BIT_AND_EXPR
     because we may be able to derive a useful range even if one of
     the operands is VR_VARYING or symbolic range.  Similarly for
     divisions.  TODO, we may be able to derive anti-ranges in
     some cases.  */
  if (code != BIT_AND_EXPR
      && code != BIT_IOR_EXPR
      && code != TRUNC_DIV_EXPR
      && code != FLOOR_DIV_EXPR
      && code != CEIL_DIV_EXPR
      && code != EXACT_DIV_EXPR
      && code != ROUND_DIV_EXPR
      && code != TRUNC_MOD_EXPR
      && (vr0.type == VR_VARYING
	  || vr1.type == VR_VARYING
	  || vr0.type != vr1.type
	  || symbolic_range_p (&vr0)
	  || symbolic_range_p (&vr1)))
    {
      set_value_range_to_varying (vr);
      return;
    }

  /* Now evaluate the expression to determine the new range.  */
  if (POINTER_TYPE_P (expr_type))
    {
      if (code == MIN_EXPR || code == MAX_EXPR)
	{
	  /* For MIN/MAX expressions with pointers, we only care about
	     nullness, if both are non null, then the result is nonnull.
	     If both are null, then the result is null. Otherwise they
	     are varying.  */
	  if (range_is_nonnull (&vr0) && range_is_nonnull (&vr1))
	    set_value_range_to_nonnull (vr, expr_type);
	  else if (range_is_null (&vr0) && range_is_null (&vr1))
	    set_value_range_to_null (vr, expr_type);
	  else
	    set_value_range_to_varying (vr);
	}
      else if (code == POINTER_PLUS_EXPR)
	{
	  /* For pointer types, we are really only interested in asserting
	     whether the expression evaluates to non-NULL.  */
	  if (range_is_nonnull (&vr0) || range_is_nonnull (&vr1))
	    set_value_range_to_nonnull (vr, expr_type);
	  else if (range_is_null (&vr0) && range_is_null (&vr1))
	    set_value_range_to_null (vr, expr_type);
	  else
	    set_value_range_to_varying (vr);
	}
      else if (code == BIT_AND_EXPR)
	{
	  /* For pointer types, we are really only interested in asserting
	     whether the expression evaluates to non-NULL.  */
	  if (range_is_nonnull (&vr0) && range_is_nonnull (&vr1))
	    set_value_range_to_nonnull (vr, expr_type);
	  else if (range_is_null (&vr0) || range_is_null (&vr1))
	    set_value_range_to_null (vr, expr_type);
	  else
	    set_value_range_to_varying (vr);
	}
      else
	set_value_range_to_varying (vr);

      return;
    }

  /* For integer ranges, apply the operation to each end of the
     range and see what we end up with.  */
  if (code == PLUS_EXPR || code == MINUS_EXPR)
    {
      /* If we have a PLUS_EXPR with two VR_RANGE integer constant
         ranges compute the precise range for such case if possible.  */
      if (range_int_cst_p (&vr0)
	  && range_int_cst_p (&vr1)
	  /* We need as many bits as the possibly unsigned inputs.  */
	  && TYPE_PRECISION (expr_type) <= HOST_BITS_PER_DOUBLE_INT)
	{
	  double_int min0 = tree_to_double_int (vr0.min);
	  double_int max0 = tree_to_double_int (vr0.max);
	  double_int min1 = tree_to_double_int (vr1.min);
	  double_int max1 = tree_to_double_int (vr1.max);
	  bool uns = TYPE_UNSIGNED (expr_type);
	  double_int type_min
	    = double_int::min_value (TYPE_PRECISION (expr_type), uns);
	  double_int type_max
	    = double_int::max_value (TYPE_PRECISION (expr_type), uns);
	  double_int dmin, dmax;
	  int min_ovf = 0;
	  int max_ovf = 0;

	  if (code == PLUS_EXPR)
	    {
	      dmin = min0 + min1;
	      dmax = max0 + max1;

	      /* Check for overflow in double_int.  */
	      if (min1.cmp (double_int_zero, uns) != dmin.cmp (min0, uns))
		min_ovf = min0.cmp (dmin, uns);
	      if (max1.cmp (double_int_zero, uns) != dmax.cmp (max0, uns))
		max_ovf = max0.cmp (dmax, uns);
	    }
	  else /* if (code == MINUS_EXPR) */
	    {
	      dmin = min0 - max1;
	      dmax = max0 - min1;

	      if (double_int_zero.cmp (max1, uns) != dmin.cmp (min0, uns))
		min_ovf = min0.cmp (max1, uns);
	      if (double_int_zero.cmp (min1, uns) != dmax.cmp (max0, uns))
		max_ovf = max0.cmp (min1, uns);
	    }

	  /* For non-wrapping arithmetic look at possibly smaller
	     value-ranges of the type.  */
	  if (!TYPE_OVERFLOW_WRAPS (expr_type))
	    {
	      if (vrp_val_min (expr_type))
		type_min = tree_to_double_int (vrp_val_min (expr_type));
	      if (vrp_val_max (expr_type))
		type_max = tree_to_double_int (vrp_val_max (expr_type));
	    }

	  /* Check for type overflow.  */
	  if (min_ovf == 0)
	    {
	      if (dmin.cmp (type_min, uns) == -1)
		min_ovf = -1;
	      else if (dmin.cmp (type_max, uns) == 1)
		min_ovf = 1;
	    }
	  if (max_ovf == 0)
	    {
	      if (dmax.cmp (type_min, uns) == -1)
		max_ovf = -1;
	      else if (dmax.cmp (type_max, uns) == 1)
		max_ovf = 1;
	    }

	  if (TYPE_OVERFLOW_WRAPS (expr_type))
	    {
	      /* If overflow wraps, truncate the values and adjust the
		 range kind and bounds appropriately.  */
	      double_int tmin
		= dmin.ext (TYPE_PRECISION (expr_type), uns);
	      double_int tmax
		= dmax.ext (TYPE_PRECISION (expr_type), uns);
	      if (min_ovf == max_ovf)
		{
		  /* No overflow or both overflow or underflow.  The
		     range kind stays VR_RANGE.  */
		  min = double_int_to_tree (expr_type, tmin);
		  max = double_int_to_tree (expr_type, tmax);
		}
	      else if (min_ovf == -1
		       && max_ovf == 1)
		{
		  /* Underflow and overflow, drop to VR_VARYING.  */
		  set_value_range_to_varying (vr);
		  return;
		}
	      else
		{
		  /* Min underflow or max overflow.  The range kind
		     changes to VR_ANTI_RANGE.  */
		  bool covers = false;
		  double_int tem = tmin;
		  gcc_assert ((min_ovf == -1 && max_ovf == 0)
			      || (max_ovf == 1 && min_ovf == 0));
		  type = VR_ANTI_RANGE;
		  tmin = tmax + double_int_one;
		  if (tmin.cmp (tmax, uns) < 0)
		    covers = true;
		  tmax = tem + double_int_minus_one;
		  if (tmax.cmp (tem, uns) > 0)
		    covers = true;
		  /* If the anti-range would cover nothing, drop to varying.
		     Likewise if the anti-range bounds are outside of the
		     types values.  */
		  if (covers || tmin.cmp (tmax, uns) > 0)
		    {
		      set_value_range_to_varying (vr);
		      return;
		    }
		  min = double_int_to_tree (expr_type, tmin);
		  max = double_int_to_tree (expr_type, tmax);
		}
	    }
	  else
	    {
	      /* If overflow does not wrap, saturate to the types min/max
	         value.  */
	      if (min_ovf == -1)
		{
		  if (needs_overflow_infinity (expr_type)
		      && supports_overflow_infinity (expr_type))
		    min = negative_overflow_infinity (expr_type);
		  else
		    min = double_int_to_tree (expr_type, type_min);
		}
	      else if (min_ovf == 1)
		{
		  if (needs_overflow_infinity (expr_type)
		      && supports_overflow_infinity (expr_type))
		    min = positive_overflow_infinity (expr_type);
		  else
		    min = double_int_to_tree (expr_type, type_max);
		}
	      else
		min = double_int_to_tree (expr_type, dmin);

	      if (max_ovf == -1)
		{
		  if (needs_overflow_infinity (expr_type)
		      && supports_overflow_infinity (expr_type))
		    max = negative_overflow_infinity (expr_type);
		  else
		    max = double_int_to_tree (expr_type, type_min);
		}
	      else if (max_ovf == 1)
		{
		  if (needs_overflow_infinity (expr_type)
		      && supports_overflow_infinity (expr_type))
		    max = positive_overflow_infinity (expr_type);
		  else
		    max = double_int_to_tree (expr_type, type_max);
		}
	      else
		max = double_int_to_tree (expr_type, dmax);
	    }
	  if (needs_overflow_infinity (expr_type)
	      && supports_overflow_infinity (expr_type))
	    {
	      if (is_negative_overflow_infinity (vr0.min)
		  || (code == PLUS_EXPR
		      ? is_negative_overflow_infinity (vr1.min)
		      : is_positive_overflow_infinity (vr1.max)))
		min = negative_overflow_infinity (expr_type);
	      if (is_positive_overflow_infinity (vr0.max)
		  || (code == PLUS_EXPR
		      ? is_positive_overflow_infinity (vr1.max)
		      : is_negative_overflow_infinity (vr1.min)))
		max = positive_overflow_infinity (expr_type);
	    }
	}
      else
	{
	  /* For other cases, for example if we have a PLUS_EXPR with two
	     VR_ANTI_RANGEs, drop to VR_VARYING.  It would take more effort
	     to compute a precise range for such a case.
	     ???  General even mixed range kind operations can be expressed
	     by for example transforming ~[3, 5] + [1, 2] to range-only
	     operations and a union primitive:
	       [-INF, 2] + [1, 2]  U  [5, +INF] + [1, 2]
	           [-INF+1, 4]     U    [6, +INF(OVF)]
	     though usually the union is not exactly representable with
	     a single range or anti-range as the above is
		 [-INF+1, +INF(OVF)] intersected with ~[5, 5]
	     but one could use a scheme similar to equivalences for this. */
	  set_value_range_to_varying (vr);
	  return;
	}
    }
  else if (code == MIN_EXPR
	   || code == MAX_EXPR)
    {
      if (vr0.type == VR_ANTI_RANGE)
	{
	  /* For MIN_EXPR and MAX_EXPR with two VR_ANTI_RANGEs,
	     the resulting VR_ANTI_RANGE is the same - intersection
	     of the two ranges.  */
	  min = vrp_int_const_binop (MAX_EXPR, vr0.min, vr1.min);
	  max = vrp_int_const_binop (MIN_EXPR, vr0.max, vr1.max);
	}
      else
	{
	  /* For operations that make the resulting range directly
	     proportional to the original ranges, apply the operation to
	     the same end of each range.  */
	  min = vrp_int_const_binop (code, vr0.min, vr1.min);
	  max = vrp_int_const_binop (code, vr0.max, vr1.max);
	}
    }
  else if (code == MULT_EXPR)
    {
      /* Fancy code so that with unsigned, [-3,-1]*[-3,-1] does not
	 drop to varying.  */
      if (range_int_cst_p (&vr0)
	  && range_int_cst_p (&vr1)
	  && TYPE_OVERFLOW_WRAPS (expr_type))
	{
	  double_int min0, max0, min1, max1, sizem1, size;
	  double_int prod0l, prod0h, prod1l, prod1h,
		     prod2l, prod2h, prod3l, prod3h;
	  bool uns0, uns1, uns;

	  sizem1 = double_int::max_value (TYPE_PRECISION (expr_type), true);
	  size = sizem1 + double_int_one;

	  min0 = tree_to_double_int (vr0.min);
	  max0 = tree_to_double_int (vr0.max);
	  min1 = tree_to_double_int (vr1.min);
	  max1 = tree_to_double_int (vr1.max);

	  uns0 = TYPE_UNSIGNED (expr_type);
	  uns1 = uns0;

	  /* Canonicalize the intervals.  */
	  if (TYPE_UNSIGNED (expr_type))
	    {
	      double_int min2 = size - min0;
	      if (!min2.is_zero () && min2.cmp (max0, true) < 0)
		{
		  min0 = -min2;
		  max0 -= size;
		  uns0 = false;
		}

	      min2 = size - min1;
	      if (!min2.is_zero () && min2.cmp (max1, true) < 0)
		{
		  min1 = -min2;
		  max1 -= size;
		  uns1 = false;
		}
	    }
	  uns = uns0 & uns1;

	  bool overflow;
	  prod0l = min0.wide_mul_with_sign (min1, true, &prod0h, &overflow);
	  if (!uns0 && min0.is_negative ())
	    prod0h -= min1;
	  if (!uns1 && min1.is_negative ())
	    prod0h -= min0;

	  prod1l = min0.wide_mul_with_sign (max1, true, &prod1h, &overflow);
	  if (!uns0 && min0.is_negative ())
	    prod1h -= max1;
	  if (!uns1 && max1.is_negative ())
	    prod1h -= min0;

	  prod2l = max0.wide_mul_with_sign (min1, true, &prod2h, &overflow);
	  if (!uns0 && max0.is_negative ())
	    prod2h -= min1;
	  if (!uns1 && min1.is_negative ())
	    prod2h -= max0;

	  prod3l = max0.wide_mul_with_sign (max1, true, &prod3h, &overflow);
	  if (!uns0 && max0.is_negative ())
	    prod3h -= max1;
	  if (!uns1 && max1.is_negative ())
	    prod3h -= max0;

	  /* Sort the 4 products.  */
	  quad_int_pair_sort (&prod0l, &prod0h, &prod3l, &prod3h, uns);
	  quad_int_pair_sort (&prod1l, &prod1h, &prod2l, &prod2h, uns);
	  quad_int_pair_sort (&prod0l, &prod0h, &prod1l, &prod1h, uns);
	  quad_int_pair_sort (&prod2l, &prod2h, &prod3l, &prod3h, uns);

	  /* Max - min.  */
	  if (prod0l.is_zero ())
	    {
	      prod1l = double_int_zero;
	      prod1h = -prod0h;
	    }
	  else
	    {
	      prod1l = -prod0l;
	      prod1h = ~prod0h;
	    }
	  prod2l = prod3l + prod1l;
	  prod2h = prod3h + prod1h;
	  if (prod2l.ult (prod3l))
	    prod2h += double_int_one; /* carry */

	  if (!prod2h.is_zero ()
	      || prod2l.cmp (sizem1, true) >= 0)
	    {
	      /* the range covers all values.  */
	      set_value_range_to_varying (vr);
	      return;
	    }

	  /* The following should handle the wrapping and selecting
	     VR_ANTI_RANGE for us.  */
	  min = double_int_to_tree (expr_type, prod0l);
	  max = double_int_to_tree (expr_type, prod3l);
	  set_and_canonicalize_value_range (vr, VR_RANGE, min, max, NULL);
	  return;
	}

      /* If we have an unsigned MULT_EXPR with two VR_ANTI_RANGEs,
	 drop to VR_VARYING.  It would take more effort to compute a
	 precise range for such a case.  For example, if we have
	 op0 == 65536 and op1 == 65536 with their ranges both being
	 ~[0,0] on a 32-bit machine, we would have op0 * op1 == 0, so
	 we cannot claim that the product is in ~[0,0].  Note that we
	 are guaranteed to have vr0.type == vr1.type at this
	 point.  */
      if (vr0.type == VR_ANTI_RANGE
	  && !TYPE_OVERFLOW_UNDEFINED (expr_type))
	{
	  set_value_range_to_varying (vr);
	  return;
	}

      extract_range_from_multiplicative_op_1 (vr, code, &vr0, &vr1);
      return;
    }
  else if (code == RSHIFT_EXPR
	   || code == LSHIFT_EXPR)
    {
      /* If we have a RSHIFT_EXPR with any shift values outside [0..prec-1],
	 then drop to VR_VARYING.  Outside of this range we get undefined
	 behavior from the shift operation.  We cannot even trust
	 SHIFT_COUNT_TRUNCATED at this stage, because that applies to rtl
	 shifts, and the operation at the tree level may be widened.  */
      if (range_int_cst_p (&vr1)
	  && compare_tree_int (vr1.min, 0) >= 0
	  && compare_tree_int (vr1.max, TYPE_PRECISION (expr_type)) == -1)
	{
	  if (code == RSHIFT_EXPR)
	    {
	      extract_range_from_multiplicative_op_1 (vr, code, &vr0, &vr1);
	      return;
	    }
	  /* We can map lshifts by constants to MULT_EXPR handling.  */
	  else if (code == LSHIFT_EXPR
		   && range_int_cst_singleton_p (&vr1))
	    {
	      bool saved_flag_wrapv;
	      value_range_t vr1p = VR_INITIALIZER;
	      vr1p.type = VR_RANGE;
	      vr1p.min
		= double_int_to_tree (expr_type,
				      double_int_one
				      .llshift (TREE_INT_CST_LOW (vr1.min),
					        TYPE_PRECISION (expr_type)));
	      vr1p.max = vr1p.min;
	      /* We have to use a wrapping multiply though as signed overflow
		 on lshifts is implementation defined in C89.  */
	      saved_flag_wrapv = flag_wrapv;
	      flag_wrapv = 1;
	      extract_range_from_binary_expr_1 (vr, MULT_EXPR, expr_type,
						&vr0, &vr1p);
	      flag_wrapv = saved_flag_wrapv;
	      return;
	    }
	  else if (code == LSHIFT_EXPR
		   && range_int_cst_p (&vr0))
	    {
	      int prec = TYPE_PRECISION (expr_type);
	      int overflow_pos = prec;
	      int bound_shift;
	      double_int bound, complement, low_bound, high_bound;
	      bool uns = TYPE_UNSIGNED (expr_type);
	      bool in_bounds = false;

	      if (!uns)
		overflow_pos -= 1;

	      bound_shift = overflow_pos - TREE_INT_CST_LOW (vr1.max);
	      /* If bound_shift == HOST_BITS_PER_DOUBLE_INT, the llshift can
		 overflow.  However, for that to happen, vr1.max needs to be
		 zero, which means vr1 is a singleton range of zero, which
		 means it should be handled by the previous LSHIFT_EXPR
		 if-clause.  */
	      bound = double_int_one.llshift (bound_shift, prec);
	      complement = ~(bound - double_int_one);

	      if (uns)
		{
		  low_bound = bound;
		  high_bound = complement.zext (prec);
		  if (tree_to_double_int (vr0.max).ult (low_bound))
		    {
		      /* [5, 6] << [1, 2] == [10, 24].  */
		      /* We're shifting out only zeroes, the value increases
			 monotonically.  */
		      in_bounds = true;
		    }
		  else if (high_bound.ult (tree_to_double_int (vr0.min)))
		    {
		      /* [0xffffff00, 0xffffffff] << [1, 2]
		         == [0xfffffc00, 0xfffffffe].  */
		      /* We're shifting out only ones, the value decreases
			 monotonically.  */
		      in_bounds = true;
		    }
		}
	      else
		{
		  /* [-1, 1] << [1, 2] == [-4, 4].  */
		  low_bound = complement.sext (prec);
		  high_bound = bound;
		  if (tree_to_double_int (vr0.max).slt (high_bound)
		      && low_bound.slt (tree_to_double_int (vr0.min)))
		    {
		      /* For non-negative numbers, we're shifting out only
			 zeroes, the value increases monotonically.
			 For negative numbers, we're shifting out only ones, the
			 value decreases monotomically.  */
		      in_bounds = true;
		    }
		}

	      if (in_bounds)
		{
		  extract_range_from_multiplicative_op_1 (vr, code, &vr0, &vr1);
		  return;
		}
	    }
	}
      set_value_range_to_varying (vr);
      return;
    }
  else if (code == TRUNC_DIV_EXPR
	   || code == FLOOR_DIV_EXPR
	   || code == CEIL_DIV_EXPR
	   || code == EXACT_DIV_EXPR
	   || code == ROUND_DIV_EXPR)
    {
      if (vr0.type != VR_RANGE || symbolic_range_p (&vr0))
	{
	  /* For division, if op1 has VR_RANGE but op0 does not, something
	     can be deduced just from that range.  Say [min, max] / [4, max]
	     gives [min / 4, max / 4] range.  */
	  if (vr1.type == VR_RANGE
	      && !symbolic_range_p (&vr1)
	      && range_includes_zero_p (vr1.min, vr1.max) == 0)
	    {
	      vr0.type = type = VR_RANGE;
	      vr0.min = vrp_val_min (expr_type);
	      vr0.max = vrp_val_max (expr_type);
	    }
	  else
	    {
	      set_value_range_to_varying (vr);
	      return;
	    }
	}

      /* For divisions, if flag_non_call_exceptions is true, we must
	 not eliminate a division by zero.  */
      if (cfun->can_throw_non_call_exceptions
	  && (vr1.type != VR_RANGE
	      || range_includes_zero_p (vr1.min, vr1.max) != 0))
	{
	  set_value_range_to_varying (vr);
	  return;
	}

      /* For divisions, if op0 is VR_RANGE, we can deduce a range
	 even if op1 is VR_VARYING, VR_ANTI_RANGE, symbolic or can
	 include 0.  */
      if (vr0.type == VR_RANGE
	  && (vr1.type != VR_RANGE
	      || range_includes_zero_p (vr1.min, vr1.max) != 0))
	{
	  tree zero = build_int_cst (TREE_TYPE (vr0.min), 0);
	  int cmp;

	  min = NULL_TREE;
	  max = NULL_TREE;
	  if (TYPE_UNSIGNED (expr_type)
	      || value_range_nonnegative_p (&vr1))
	    {
	      /* For unsigned division or when divisor is known
		 to be non-negative, the range has to cover
		 all numbers from 0 to max for positive max
		 and all numbers from min to 0 for negative min.  */
	      cmp = compare_values (vr0.max, zero);
	      if (cmp == -1)
		max = zero;
	      else if (cmp == 0 || cmp == 1)
		max = vr0.max;
	      else
		type = VR_VARYING;
	      cmp = compare_values (vr0.min, zero);
	      if (cmp == 1)
		min = zero;
	      else if (cmp == 0 || cmp == -1)
		min = vr0.min;
	      else
		type = VR_VARYING;
	    }
	  else
	    {
	      /* Otherwise the range is -max .. max or min .. -min
		 depending on which bound is bigger in absolute value,
		 as the division can change the sign.  */
	      abs_extent_range (vr, vr0.min, vr0.max);
	      return;
	    }
	  if (type == VR_VARYING)
	    {
	      set_value_range_to_varying (vr);
	      return;
	    }
	}
      else
	{
	  extract_range_from_multiplicative_op_1 (vr, code, &vr0, &vr1);
	  return;
	}
    }
  else if (code == TRUNC_MOD_EXPR)
    {
      if (vr1.type != VR_RANGE
	  || range_includes_zero_p (vr1.min, vr1.max) != 0
	  || vrp_val_is_min (vr1.min))
	{
	  set_value_range_to_varying (vr);
	  return;
	}
      type = VR_RANGE;
      /* Compute MAX <|vr1.min|, |vr1.max|> - 1.  */
      max = fold_unary_to_constant (ABS_EXPR, expr_type, vr1.min);
      if (tree_int_cst_lt (max, vr1.max))
	max = vr1.max;
      max = int_const_binop (MINUS_EXPR, max, integer_one_node);
      /* If the dividend is non-negative the modulus will be
	 non-negative as well.  */
      if (TYPE_UNSIGNED (expr_type)
	  || value_range_nonnegative_p (&vr0))
	min = build_int_cst (TREE_TYPE (max), 0);
      else
	min = fold_unary_to_constant (NEGATE_EXPR, expr_type, max);
    }
  else if (code == BIT_AND_EXPR || code == BIT_IOR_EXPR || code == BIT_XOR_EXPR)
    {
      bool int_cst_range0, int_cst_range1;
      double_int may_be_nonzero0, may_be_nonzero1;
      double_int must_be_nonzero0, must_be_nonzero1;

      int_cst_range0 = zero_nonzero_bits_from_vr (&vr0, &may_be_nonzero0,
						  &must_be_nonzero0);
      int_cst_range1 = zero_nonzero_bits_from_vr (&vr1, &may_be_nonzero1,
						  &must_be_nonzero1);

      type = VR_RANGE;
      if (code == BIT_AND_EXPR)
	{
	  double_int dmax;
	  min = double_int_to_tree (expr_type,
				    must_be_nonzero0 & must_be_nonzero1);
	  dmax = may_be_nonzero0 & may_be_nonzero1;
	  /* If both input ranges contain only negative values we can
	     truncate the result range maximum to the minimum of the
	     input range maxima.  */
	  if (int_cst_range0 && int_cst_range1
	      && tree_int_cst_sgn (vr0.max) < 0
	      && tree_int_cst_sgn (vr1.max) < 0)
	    {
	      dmax = dmax.min (tree_to_double_int (vr0.max),
				     TYPE_UNSIGNED (expr_type));
	      dmax = dmax.min (tree_to_double_int (vr1.max),
				     TYPE_UNSIGNED (expr_type));
	    }
	  /* If either input range contains only non-negative values
	     we can truncate the result range maximum to the respective
	     maximum of the input range.  */
	  if (int_cst_range0 && tree_int_cst_sgn (vr0.min) >= 0)
	    dmax = dmax.min (tree_to_double_int (vr0.max),
				   TYPE_UNSIGNED (expr_type));
	  if (int_cst_range1 && tree_int_cst_sgn (vr1.min) >= 0)
	    dmax = dmax.min (tree_to_double_int (vr1.max),
				   TYPE_UNSIGNED (expr_type));
	  max = double_int_to_tree (expr_type, dmax);
	}
      else if (code == BIT_IOR_EXPR)
	{
	  double_int dmin;
	  max = double_int_to_tree (expr_type,
				    may_be_nonzero0 | may_be_nonzero1);
	  dmin = must_be_nonzero0 | must_be_nonzero1;
	  /* If the input ranges contain only positive values we can
	     truncate the minimum of the result range to the maximum
	     of the input range minima.  */
	  if (int_cst_range0 && int_cst_range1
	      && tree_int_cst_sgn (vr0.min) >= 0
	      && tree_int_cst_sgn (vr1.min) >= 0)
	    {
	      dmin = dmin.max (tree_to_double_int (vr0.min),
			       TYPE_UNSIGNED (expr_type));
	      dmin = dmin.max (tree_to_double_int (vr1.min),
			       TYPE_UNSIGNED (expr_type));
	    }
	  /* If either input range contains only negative values
	     we can truncate the minimum of the result range to the
	     respective minimum range.  */
	  if (int_cst_range0 && tree_int_cst_sgn (vr0.max) < 0)
	    dmin = dmin.max (tree_to_double_int (vr0.min),
			     TYPE_UNSIGNED (expr_type));
	  if (int_cst_range1 && tree_int_cst_sgn (vr1.max) < 0)
	    dmin = dmin.max (tree_to_double_int (vr1.min),
			     TYPE_UNSIGNED (expr_type));
	  min = double_int_to_tree (expr_type, dmin);
	}
      else if (code == BIT_XOR_EXPR)
	{
	  double_int result_zero_bits, result_one_bits;
	  result_zero_bits = (must_be_nonzero0 & must_be_nonzero1)
			     | ~(may_be_nonzero0 | may_be_nonzero1);
	  result_one_bits = must_be_nonzero0.and_not (may_be_nonzero1)
			    | must_be_nonzero1.and_not (may_be_nonzero0);
	  max = double_int_to_tree (expr_type, ~result_zero_bits);
	  min = double_int_to_tree (expr_type, result_one_bits);
	  /* If the range has all positive or all negative values the
	     result is better than VARYING.  */
	  if (tree_int_cst_sgn (min) < 0
	      || tree_int_cst_sgn (max) >= 0)
	    ;
	  else
	    max = min = NULL_TREE;
	}
    }
  else
    gcc_unreachable ();

  /* If either MIN or MAX overflowed, then set the resulting range to
     VARYING.  But we do accept an overflow infinity
     representation.  */
  if (min == NULL_TREE
      || !is_gimple_min_invariant (min)
      || (TREE_OVERFLOW (min) && !is_overflow_infinity (min))
      || max == NULL_TREE
      || !is_gimple_min_invariant (max)
      || (TREE_OVERFLOW (max) && !is_overflow_infinity (max)))
    {
      set_value_range_to_varying (vr);
      return;
    }

  /* We punt if:
     1) [-INF, +INF]
     2) [-INF, +-INF(OVF)]
     3) [+-INF(OVF), +INF]
     4) [+-INF(OVF), +-INF(OVF)]
     We learn nothing when we have INF and INF(OVF) on both sides.
     Note that we do accept [-INF, -INF] and [+INF, +INF] without
     overflow.  */
  if ((vrp_val_is_min (min) || is_overflow_infinity (min))
      && (vrp_val_is_max (max) || is_overflow_infinity (max)))
    {
      set_value_range_to_varying (vr);
      return;
    }

  cmp = compare_values (min, max);
  if (cmp == -2 || cmp == 1)
    {
      /* If the new range has its limits swapped around (MIN > MAX),
	 then the operation caused one of them to wrap around, mark
	 the new range VARYING.  */
      set_value_range_to_varying (vr);
    }
  else
    set_value_range (vr, type, min, max, NULL);
}

/* Extract range information from a binary expression OP0 CODE OP1 based on
   the ranges of each of its operands with resulting type EXPR_TYPE.
   The resulting range is stored in *VR.  */

static void
extract_range_from_binary_expr (value_range_t *vr,
				enum tree_code code,
				tree expr_type, tree op0, tree op1)
{
  value_range_t vr0 = VR_INITIALIZER;
  value_range_t vr1 = VR_INITIALIZER;

  /* Get value ranges for each operand.  For constant operands, create
     a new value range with the operand to simplify processing.  */
  if (TREE_CODE (op0) == SSA_NAME)
    vr0 = *(get_value_range (op0));
  else if (is_gimple_min_invariant (op0))
    set_value_range_to_value (&vr0, op0, NULL);
  else
    set_value_range_to_varying (&vr0);

  if (TREE_CODE (op1) == SSA_NAME)
    vr1 = *(get_value_range (op1));
  else if (is_gimple_min_invariant (op1))
    set_value_range_to_value (&vr1, op1, NULL);
  else
    set_value_range_to_varying (&vr1);

  extract_range_from_binary_expr_1 (vr, code, expr_type, &vr0, &vr1);
}

/* Extract range information from a unary operation CODE based on
   the range of its operand *VR0 with type OP0_TYPE with resulting type TYPE.
   The The resulting range is stored in *VR.  */

static void
extract_range_from_unary_expr_1 (value_range_t *vr,
				 enum tree_code code, tree type,
				 value_range_t *vr0_, tree op0_type)
{
  value_range_t vr0 = *vr0_, vrtem0 = VR_INITIALIZER, vrtem1 = VR_INITIALIZER;

  /* VRP only operates on integral and pointer types.  */
  if (!(INTEGRAL_TYPE_P (op0_type)
	|| POINTER_TYPE_P (op0_type))
      || !(INTEGRAL_TYPE_P (type)
	   || POINTER_TYPE_P (type)))
    {
      set_value_range_to_varying (vr);
      return;
    }

  /* If VR0 is UNDEFINED, so is the result.  */
  if (vr0.type == VR_UNDEFINED)
    {
      set_value_range_to_undefined (vr);
      return;
    }

  /* Handle operations that we express in terms of others.  */
  if (code == PAREN_EXPR)
    {
      /* PAREN_EXPR is a simple copy.  */
      copy_value_range (vr, &vr0);
      return;
    }
  else if (code == NEGATE_EXPR)
    {
      /* -X is simply 0 - X, so re-use existing code that also handles
         anti-ranges fine.  */
      value_range_t zero = VR_INITIALIZER;
      set_value_range_to_value (&zero, build_int_cst (type, 0), NULL);
      extract_range_from_binary_expr_1 (vr, MINUS_EXPR, type, &zero, &vr0);
      return;
    }
  else if (code == BIT_NOT_EXPR)
    {
      /* ~X is simply -1 - X, so re-use existing code that also handles
         anti-ranges fine.  */
      value_range_t minusone = VR_INITIALIZER;
      set_value_range_to_value (&minusone, build_int_cst (type, -1), NULL);
      extract_range_from_binary_expr_1 (vr, MINUS_EXPR,
					type, &minusone, &vr0);
      return;
    }

  /* Now canonicalize anti-ranges to ranges when they are not symbolic
     and express op ~[]  as (op []') U (op []'').  */
  if (vr0.type == VR_ANTI_RANGE
      && ranges_from_anti_range (&vr0, &vrtem0, &vrtem1))
    {
      extract_range_from_unary_expr_1 (vr, code, type, &vrtem0, op0_type);
      if (vrtem1.type != VR_UNDEFINED)
	{
	  value_range_t vrres = VR_INITIALIZER;
	  extract_range_from_unary_expr_1 (&vrres, code, type,
					   &vrtem1, op0_type);
	  vrp_meet (vr, &vrres);
	}
      return;
    }

  if (CONVERT_EXPR_CODE_P (code))
    {
      tree inner_type = op0_type;
      tree outer_type = type;

      /* If the expression evaluates to a pointer, we are only interested in
	 determining if it evaluates to NULL [0, 0] or non-NULL (~[0, 0]).  */
      if (POINTER_TYPE_P (type))
	{
	  if (range_is_nonnull (&vr0))
	    set_value_range_to_nonnull (vr, type);
	  else if (range_is_null (&vr0))
	    set_value_range_to_null (vr, type);
	  else
	    set_value_range_to_varying (vr);
	  return;
	}

      /* If VR0 is varying and we increase the type precision, assume
	 a full range for the following transformation.  */
      if (vr0.type == VR_VARYING
	  && INTEGRAL_TYPE_P (inner_type)
	  && TYPE_PRECISION (inner_type) < TYPE_PRECISION (outer_type))
	{
	  vr0.type = VR_RANGE;
	  vr0.min = TYPE_MIN_VALUE (inner_type);
	  vr0.max = TYPE_MAX_VALUE (inner_type);
	}

      /* If VR0 is a constant range or anti-range and the conversion is
	 not truncating we can convert the min and max values and
	 canonicalize the resulting range.  Otherwise we can do the
	 conversion if the size of the range is less than what the
	 precision of the target type can represent and the range is
	 not an anti-range.  */
      if ((vr0.type == VR_RANGE
	   || vr0.type == VR_ANTI_RANGE)
	  && TREE_CODE (vr0.min) == INTEGER_CST
	  && TREE_CODE (vr0.max) == INTEGER_CST
	  && (!is_overflow_infinity (vr0.min)
	      || (vr0.type == VR_RANGE
		  && TYPE_PRECISION (outer_type) > TYPE_PRECISION (inner_type)
		  && needs_overflow_infinity (outer_type)
		  && supports_overflow_infinity (outer_type)))
	  && (!is_overflow_infinity (vr0.max)
	      || (vr0.type == VR_RANGE
		  && TYPE_PRECISION (outer_type) > TYPE_PRECISION (inner_type)
		  && needs_overflow_infinity (outer_type)
		  && supports_overflow_infinity (outer_type)))
	  && (TYPE_PRECISION (outer_type) >= TYPE_PRECISION (inner_type)
	      || (vr0.type == VR_RANGE
		  && integer_zerop (int_const_binop (RSHIFT_EXPR,
		       int_const_binop (MINUS_EXPR, vr0.max, vr0.min),
		         size_int (TYPE_PRECISION (outer_type)))))))
	{
	  tree new_min, new_max;
	  if (is_overflow_infinity (vr0.min))
	    new_min = negative_overflow_infinity (outer_type);
	  else
	    new_min = force_fit_type_double (outer_type,
					     tree_to_double_int (vr0.min),
					     0, false);
	  if (is_overflow_infinity (vr0.max))
	    new_max = positive_overflow_infinity (outer_type);
	  else
	    new_max = force_fit_type_double (outer_type,
					     tree_to_double_int (vr0.max),
					     0, false);
	  set_and_canonicalize_value_range (vr, vr0.type,
					    new_min, new_max, NULL);
	  return;
	}

      set_value_range_to_varying (vr);
      return;
    }
  else if (code == ABS_EXPR)
    {
      tree min, max;
      int cmp;

      /* Pass through vr0 in the easy cases.  */
      if (TYPE_UNSIGNED (type)
	  || value_range_nonnegative_p (&vr0))
	{
	  copy_value_range (vr, &vr0);
	  return;
	}

      /* For the remaining varying or symbolic ranges we can't do anything
	 useful.  */
      if (vr0.type == VR_VARYING
	  || symbolic_range_p (&vr0))
	{
	  set_value_range_to_varying (vr);
	  return;
	}

      /* -TYPE_MIN_VALUE = TYPE_MIN_VALUE with flag_wrapv so we can't get a
         useful range.  */
      if (!TYPE_OVERFLOW_UNDEFINED (type)
	  && ((vr0.type == VR_RANGE
	       && vrp_val_is_min (vr0.min))
	      || (vr0.type == VR_ANTI_RANGE
		  && !vrp_val_is_min (vr0.min))))
	{
	  set_value_range_to_varying (vr);
	  return;
	}

      /* ABS_EXPR may flip the range around, if the original range
	 included negative values.  */
      if (is_overflow_infinity (vr0.min))
	min = positive_overflow_infinity (type);
      else if (!vrp_val_is_min (vr0.min))
	min = fold_unary_to_constant (code, type, vr0.min);
      else if (!needs_overflow_infinity (type))
	min = TYPE_MAX_VALUE (type);
      else if (supports_overflow_infinity (type))
	min = positive_overflow_infinity (type);
      else
	{
	  set_value_range_to_varying (vr);
	  return;
	}

      if (is_overflow_infinity (vr0.max))
	max = positive_overflow_infinity (type);
      else if (!vrp_val_is_min (vr0.max))
	max = fold_unary_to_constant (code, type, vr0.max);
      else if (!needs_overflow_infinity (type))
	max = TYPE_MAX_VALUE (type);
      else if (supports_overflow_infinity (type)
	       /* We shouldn't generate [+INF, +INF] as set_value_range
		  doesn't like this and ICEs.  */
	       && !is_positive_overflow_infinity (min))
	max = positive_overflow_infinity (type);
      else
	{
	  set_value_range_to_varying (vr);
	  return;
	}

      cmp = compare_values (min, max);

      /* If a VR_ANTI_RANGEs contains zero, then we have
	 ~[-INF, min(MIN, MAX)].  */
      if (vr0.type == VR_ANTI_RANGE)
	{
	  if (range_includes_zero_p (vr0.min, vr0.max) == 1)
	    {
	      /* Take the lower of the two values.  */
	      if (cmp != 1)
		max = min;

	      /* Create ~[-INF, min (abs(MIN), abs(MAX))]
	         or ~[-INF + 1, min (abs(MIN), abs(MAX))] when
		 flag_wrapv is set and the original anti-range doesn't include
	         TYPE_MIN_VALUE, remember -TYPE_MIN_VALUE = TYPE_MIN_VALUE.  */
	      if (TYPE_OVERFLOW_WRAPS (type))
		{
		  tree type_min_value = TYPE_MIN_VALUE (type);

		  min = (vr0.min != type_min_value
			 ? int_const_binop (PLUS_EXPR, type_min_value,
					    integer_one_node)
			 : type_min_value);
		}
	      else
		{
		  if (overflow_infinity_range_p (&vr0))
		    min = negative_overflow_infinity (type);
		  else
		    min = TYPE_MIN_VALUE (type);
		}
	    }
	  else
	    {
	      /* All else has failed, so create the range [0, INF], even for
	         flag_wrapv since TYPE_MIN_VALUE is in the original
	         anti-range.  */
	      vr0.type = VR_RANGE;
	      min = build_int_cst (type, 0);
	      if (needs_overflow_infinity (type))
		{
		  if (supports_overflow_infinity (type))
		    max = positive_overflow_infinity (type);
		  else
		    {
		      set_value_range_to_varying (vr);
		      return;
		    }
		}
	      else
		max = TYPE_MAX_VALUE (type);
	    }
	}

      /* If the range contains zero then we know that the minimum value in the
         range will be zero.  */
      else if (range_includes_zero_p (vr0.min, vr0.max) == 1)
	{
	  if (cmp == 1)
	    max = min;
	  min = build_int_cst (type, 0);
	}
      else
	{
          /* If the range was reversed, swap MIN and MAX.  */
	  if (cmp == 1)
	    {
	      tree t = min;
	      min = max;
	      max = t;
	    }
	}

      cmp = compare_values (min, max);
      if (cmp == -2 || cmp == 1)
	{
	  /* If the new range has its limits swapped around (MIN > MAX),
	     then the operation caused one of them to wrap around, mark
	     the new range VARYING.  */
	  set_value_range_to_varying (vr);
	}
      else
	set_value_range (vr, vr0.type, min, max, NULL);
      return;
    }

  /* For unhandled operations fall back to varying.  */
  set_value_range_to_varying (vr);
  return;
}


/* Extract range information from a unary expression CODE OP0 based on
   the range of its operand with resulting type TYPE.
   The resulting range is stored in *VR.  */

static void
extract_range_from_unary_expr (value_range_t *vr, enum tree_code code,
			       tree type, tree op0)
{
  value_range_t vr0 = VR_INITIALIZER;

  /* Get value ranges for the operand.  For constant operands, create
     a new value range with the operand to simplify processing.  */
  if (TREE_CODE (op0) == SSA_NAME)
    vr0 = *(get_value_range (op0));
  else if (is_gimple_min_invariant (op0))
    set_value_range_to_value (&vr0, op0, NULL);
  else
    set_value_range_to_varying (&vr0);

  extract_range_from_unary_expr_1 (vr, code, type, &vr0, TREE_TYPE (op0));
}


/* Extract range information from a conditional expression STMT based on
   the ranges of each of its operands and the expression code.  */

static void
extract_range_from_cond_expr (value_range_t *vr, GIMPLE_type stmt)
{
  tree op0, op1;
  value_range_t vr0 = VR_INITIALIZER;
  value_range_t vr1 = VR_INITIALIZER;

  /* Get value ranges for each operand.  For constant operands, create
     a new value range with the operand to simplify processing.  */
  op0 = gimple_assign_rhs2 (stmt);
  if (TREE_CODE (op0) == SSA_NAME)
    vr0 = *(get_value_range (op0));
  else if (is_gimple_min_invariant (op0))
    set_value_range_to_value (&vr0, op0, NULL);
  else
    set_value_range_to_varying (&vr0);

  op1 = gimple_assign_rhs3 (stmt);
  if (TREE_CODE (op1) == SSA_NAME)
    vr1 = *(get_value_range (op1));
  else if (is_gimple_min_invariant (op1))
    set_value_range_to_value (&vr1, op1, NULL);
  else
    set_value_range_to_varying (&vr1);

  /* The resulting value range is the union of the operand ranges */
  copy_value_range (vr, &vr0);
  vrp_meet (vr, &vr1);
}


/* Extract range information from a comparison expression EXPR based
   on the range of its operand and the expression code.  */

static void
extract_range_from_comparison (value_range_t *vr, enum tree_code code,
			       tree type, tree op0, tree op1)
{
  bool sop = false;
  tree val;

  val = vrp_evaluate_conditional_warnv_with_ops (code, op0, op1, false, &sop,
  						 NULL);

  /* A disadvantage of using a special infinity as an overflow
     representation is that we lose the ability to record overflow
     when we don't have an infinity.  So we have to ignore a result
     which relies on overflow.  */

  if (val && !is_overflow_infinity (val) && !sop)
    {
      /* Since this expression was found on the RHS of an assignment,
	 its type may be different from _Bool.  Convert VAL to EXPR's
	 type.  */
      val = fold_convert (type, val);
      if (is_gimple_min_invariant (val))
	set_value_range_to_value (vr, val, vr->equiv);
      else
	set_value_range (vr, VR_RANGE, val, val, vr->equiv);
    }
  else
    /* The result of a comparison is always true or false.  */
    set_value_range_to_truthvalue (vr, type);
}

/* Try to derive a nonnegative or nonzero range out of STMT relying
   primarily on generic routines in fold in conjunction with range data.
   Store the result in *VR */

static void
extract_range_basic (value_range_t *vr, GIMPLE_type stmt)
{
  bool sop = false;
  tree type = gimple_expr_type (stmt);

  if (INTEGRAL_TYPE_P (type)
      && gimple_stmt_nonnegative_warnv_p (stmt, &sop))
    set_value_range_to_nonnegative (vr, type,
				    sop || stmt_overflow_infinity (stmt));
  else if (vrp_stmt_computes_nonzero (stmt, &sop)
	   && !sop)
    set_value_range_to_nonnull (vr, type);
  else
    set_value_range_to_varying (vr);
}


/* Try to compute a useful range out of assignment STMT and store it
   in *VR.  */

static void
extract_range_from_assignment (value_range_t *vr, GIMPLE_type stmt)
{
  enum tree_code code = gimple_assign_rhs_code (stmt);

  if (code == ASSERT_EXPR)
    extract_range_from_assert (vr, gimple_assign_rhs1 (stmt));
  else if (code == SSA_NAME)
    extract_range_from_ssa_name (vr, gimple_assign_rhs1 (stmt));
  else if (TREE_CODE_CLASS (code) == tcc_binary)
    extract_range_from_binary_expr (vr, gimple_assign_rhs_code (stmt),
				    gimple_expr_type (stmt),
				    gimple_assign_rhs1 (stmt),
				    gimple_assign_rhs2 (stmt));
  else if (TREE_CODE_CLASS (code) == tcc_unary)
    extract_range_from_unary_expr (vr, gimple_assign_rhs_code (stmt),
				   gimple_expr_type (stmt),
				   gimple_assign_rhs1 (stmt));
  else if (code == COND_EXPR)
    extract_range_from_cond_expr (vr, stmt);
  else if (TREE_CODE_CLASS (code) == tcc_comparison)
    extract_range_from_comparison (vr, gimple_assign_rhs_code (stmt),
				   gimple_expr_type (stmt),
				   gimple_assign_rhs1 (stmt),
				   gimple_assign_rhs2 (stmt));
  else if (get_gimple_rhs_class (code) == GIMPLE_SINGLE_RHS
	   && is_gimple_min_invariant (gimple_assign_rhs1 (stmt)))
    set_value_range_to_value (vr, gimple_assign_rhs1 (stmt), NULL);
  else
    set_value_range_to_varying (vr);

  if (vr->type == VR_VARYING)
    extract_range_basic (vr, stmt);
}

/* Given a range VR, a LOOP and a variable VAR, determine whether it
   would be profitable to adjust VR using scalar evolution information
   for VAR.  If so, update VR with the new limits.  */

static void
adjust_range_with_scev (value_range_t *vr, struct loop *loop,
         GIMPLE_type stmt, tree var)
{
  tree init, step, chrec, tmin, tmax, min, max, type, tem;
  enum ev_direction dir;

  /* TODO.  Don't adjust anti-ranges.  An anti-range may provide
     better opportunities than a regular range, but I'm not sure.  */
  if (vr->type == VR_ANTI_RANGE)
    return;

  chrec = instantiate_parameters (loop, analyze_scalar_evolution (loop, var));

  /* Like in PR19590, scev can return a constant function.  */
  if (is_gimple_min_invariant (chrec))
    {
      set_value_range_to_value (vr, chrec, vr->equiv);
      return;
    }

  if (TREE_CODE (chrec) != POLYNOMIAL_CHREC)
    return;

  init = initial_condition_in_loop_num (chrec, loop->num);
  tem = op_with_constant_singleton_value_range (init);
  if (tem)
    init = tem;
  step = evolution_part_in_loop_num (chrec, loop->num);
  tem = op_with_constant_singleton_value_range (step);
  if (tem)
    step = tem;

  /* If STEP is symbolic, we can't know whether INIT will be the
     minimum or maximum value in the range.  Also, unless INIT is
     a simple expression, compare_values and possibly other functions
     in tree-vrp won't be able to handle it.  */
  if (step == NULL_TREE
      || !is_gimple_min_invariant (step)
      || !valid_value_p (init))
    return;

  dir = scev_direction (chrec);
  if (/* Do not adjust ranges if we do not know whether the iv increases
	 or decreases,  ... */
      dir == EV_DIR_UNKNOWN
      /* ... or if it may wrap.  */
      || scev_probably_wraps_p (init, step, stmt, get_chrec_loop (chrec),
				true))
    return;

  /* We use TYPE_MIN_VALUE and TYPE_MAX_VALUE here instead of
     negative_overflow_infinity and positive_overflow_infinity,
     because we have concluded that the loop probably does not
     wrap.  */

  type = TREE_TYPE (var);
  if (POINTER_TYPE_P (type) || !TYPE_MIN_VALUE (type))
    tmin = lower_bound_in_type (type, type);
  else
    tmin = TYPE_MIN_VALUE (type);
  if (POINTER_TYPE_P (type) || !TYPE_MAX_VALUE (type))
    tmax = upper_bound_in_type (type, type);
  else
    tmax = TYPE_MAX_VALUE (type);

  /* Try to use estimated number of iterations for the loop to constrain the
     final value in the evolution.  */
  if (TREE_CODE (step) == INTEGER_CST
      && is_gimple_val (init)
      && (TREE_CODE (init) != SSA_NAME
	  || get_value_range (init)->type == VR_RANGE))
    {
      double_int nit;

      /* We are only entering here for loop header PHI nodes, so using
	 the number of latch executions is the correct thing to use.  */
      if (max_loop_iterations (loop, &nit))
	{
	  value_range_t maxvr = VR_INITIALIZER;
	  double_int dtmp;
	  bool unsigned_p = TYPE_UNSIGNED (TREE_TYPE (step));
	  bool overflow = false;

	  dtmp = tree_to_double_int (step)
		 .mul_with_sign (nit, unsigned_p, &overflow);
	  /* If the multiplication overflowed we can't do a meaningful
	     adjustment.  Likewise if the result doesn't fit in the type
	     of the induction variable.  For a signed type we have to
	     check whether the result has the expected signedness which
	     is that of the step as number of iterations is unsigned.  */
	  if (!overflow
	      && double_int_fits_to_tree_p (TREE_TYPE (init), dtmp)
	      && (unsigned_p
		  || ((dtmp.high ^ TREE_INT_CST_HIGH (step)) >= 0)))
	    {
	      tem = double_int_to_tree (TREE_TYPE (init), dtmp);
	      extract_range_from_binary_expr (&maxvr, PLUS_EXPR,
					      TREE_TYPE (init), init, tem);
	      /* Likewise if the addition did.  */
	      if (maxvr.type == VR_RANGE)
		{
		  tmin = maxvr.min;
		  tmax = maxvr.max;
		}
	    }
	}
    }

  if (vr->type == VR_VARYING || vr->type == VR_UNDEFINED)
    {
      min = tmin;
      max = tmax;

      /* For VARYING or UNDEFINED ranges, just about anything we get
	 from scalar evolutions should be better.  */

      if (dir == EV_DIR_DECREASES)
	max = init;
      else
	min = init;

      /* If we would create an invalid range, then just assume we
	 know absolutely nothing.  This may be over-conservative,
	 but it's clearly safe, and should happen only in unreachable
         parts of code, or for invalid programs.  */
      if (compare_values (min, max) == 1)
	return;

      set_value_range (vr, VR_RANGE, min, max, vr->equiv);
    }
  else if (vr->type == VR_RANGE)
    {
      min = vr->min;
      max = vr->max;

      if (dir == EV_DIR_DECREASES)
	{
	  /* INIT is the maximum value.  If INIT is lower than VR->MAX
	     but no smaller than VR->MIN, set VR->MAX to INIT.  */
	  if (compare_values (init, max) == -1)
	    max = init;

	  /* According to the loop information, the variable does not
	     overflow.  If we think it does, probably because of an
	     overflow due to arithmetic on a different INF value,
	     reset now.  */
	  if (is_negative_overflow_infinity (min)
	      || compare_values (min, tmin) == -1)
	    min = tmin;

	}
      else
	{
	  /* If INIT is bigger than VR->MIN, set VR->MIN to INIT.  */
	  if (compare_values (init, min) == 1)
	    min = init;

	  if (is_positive_overflow_infinity (max)
	      || compare_values (tmax, max) == -1)
	    max = tmax;
	}

      /* If we just created an invalid range with the minimum
	 greater than the maximum, we fail conservatively.
	 This should happen only in unreachable
	 parts of code, or for invalid programs.  */
      if (compare_values (min, max) == 1)
	return;

      set_value_range (vr, VR_RANGE, min, max, vr->equiv);
    }
}

/* Return true if VAR may overflow at STMT.  This checks any available
   loop information to see if we can determine that VAR does not
   overflow.  */

static bool
vrp_var_may_overflow (tree var, GIMPLE_type stmt)
{
  struct loop *l;
  tree chrec, init, step;

  if (current_loops == NULL)
    return true;

  l = loop_containing_stmt (stmt);
  if (l == NULL
      || !loop_outer (l))
    return true;

  chrec = instantiate_parameters (l, analyze_scalar_evolution (l, var));
  if (TREE_CODE (chrec) != POLYNOMIAL_CHREC)
    return true;

  init = initial_condition_in_loop_num (chrec, l->num);
  step = evolution_part_in_loop_num (chrec, l->num);

  if (step == NULL_TREE
      || !is_gimple_min_invariant (step)
      || !valid_value_p (init))
    return true;

  /* If we get here, we know something useful about VAR based on the
     loop information.  If it wraps, it may overflow.  */

  if (scev_probably_wraps_p (init, step, stmt, get_chrec_loop (chrec),
			     true))
    return true;

  if (dump_file && (dump_flags & TDF_DETAILS) != 0)
    {
      print_generic_expr (dump_file, var, 0);
      fprintf (dump_file, ": loop information indicates does not overflow\n");
    }

  return false;
}


/* Given two numeric value ranges VR0, VR1 and a comparison code COMP:

   - Return BOOLEAN_TRUE_NODE if VR0 COMP VR1 always returns true for
     all the values in the ranges.

   - Return BOOLEAN_FALSE_NODE if the comparison always returns false.

   - Return NULL_TREE if it is not always possible to determine the
     value of the comparison.

   Also set *STRICT_OVERFLOW_P to indicate whether a range with an
   overflow infinity was used in the test.  */


static tree
compare_ranges (enum tree_code comp, value_range_t *vr0, value_range_t *vr1,
		bool *strict_overflow_p)
{
  /* VARYING or UNDEFINED ranges cannot be compared.  */
  if (vr0->type == VR_VARYING
      || vr0->type == VR_UNDEFINED
      || vr1->type == VR_VARYING
      || vr1->type == VR_UNDEFINED)
    return NULL_TREE;

  /* Anti-ranges need to be handled separately.  */
  if (vr0->type == VR_ANTI_RANGE || vr1->type == VR_ANTI_RANGE)
    {
      /* If both are anti-ranges, then we cannot compute any
	 comparison.  */
      if (vr0->type == VR_ANTI_RANGE && vr1->type == VR_ANTI_RANGE)
	return NULL_TREE;

      /* These comparisons are never statically computable.  */
      if (comp == GT_EXPR
	  || comp == GE_EXPR
	  || comp == LT_EXPR
	  || comp == LE_EXPR)
	return NULL_TREE;

      /* Equality can be computed only between a range and an
	 anti-range.  ~[VAL1, VAL2] == [VAL1, VAL2] is always false.  */
      if (vr0->type == VR_RANGE)
	{
	  /* To simplify processing, make VR0 the anti-range.  */
	  value_range_t *tmp = vr0;
	  vr0 = vr1;
	  vr1 = tmp;
	}

      gcc_assert (comp == NE_EXPR || comp == EQ_EXPR);

      if (compare_values_warnv (vr0->min, vr1->min, strict_overflow_p) == 0
	  && compare_values_warnv (vr0->max, vr1->max, strict_overflow_p) == 0)
	return (comp == NE_EXPR) ? boolean_true_node : boolean_false_node;

      return NULL_TREE;
    }

  if (!usable_range_p (vr0, strict_overflow_p)
      || !usable_range_p (vr1, strict_overflow_p))
    return NULL_TREE;

  /* Simplify processing.  If COMP is GT_EXPR or GE_EXPR, switch the
     operands around and change the comparison code.  */
  if (comp == GT_EXPR || comp == GE_EXPR)
    {
      value_range_t *tmp;
      comp = (comp == GT_EXPR) ? LT_EXPR : LE_EXPR;
      tmp = vr0;
      vr0 = vr1;
      vr1 = tmp;
    }

  if (comp == EQ_EXPR)
    {
      /* Equality may only be computed if both ranges represent
	 exactly one value.  */
      if (compare_values_warnv (vr0->min, vr0->max, strict_overflow_p) == 0
	  && compare_values_warnv (vr1->min, vr1->max, strict_overflow_p) == 0)
	{
	  int cmp_min = compare_values_warnv (vr0->min, vr1->min,
					      strict_overflow_p);
	  int cmp_max = compare_values_warnv (vr0->max, vr1->max,
					      strict_overflow_p);
	  if (cmp_min == 0 && cmp_max == 0)
	    return boolean_true_node;
	  else if (cmp_min != -2 && cmp_max != -2)
	    return boolean_false_node;
	}
      /* If [V0_MIN, V1_MAX] < [V1_MIN, V1_MAX] then V0 != V1.  */
      else if (compare_values_warnv (vr0->min, vr1->max,
				     strict_overflow_p) == 1
	       || compare_values_warnv (vr1->min, vr0->max,
					strict_overflow_p) == 1)
	return boolean_false_node;

      return NULL_TREE;
    }
  else if (comp == NE_EXPR)
    {
      int cmp1, cmp2;

      /* If VR0 is completely to the left or completely to the right
	 of VR1, they are always different.  Notice that we need to
	 make sure that both comparisons yield similar results to
	 avoid comparing values that cannot be compared at
	 compile-time.  */
      cmp1 = compare_values_warnv (vr0->max, vr1->min, strict_overflow_p);
      cmp2 = compare_values_warnv (vr0->min, vr1->max, strict_overflow_p);
      if ((cmp1 == -1 && cmp2 == -1) || (cmp1 == 1 && cmp2 == 1))
	return boolean_true_node;

      /* If VR0 and VR1 represent a single value and are identical,
	 return false.  */
      else if (compare_values_warnv (vr0->min, vr0->max,
				     strict_overflow_p) == 0
	       && compare_values_warnv (vr1->min, vr1->max,
					strict_overflow_p) == 0
	       && compare_values_warnv (vr0->min, vr1->min,
					strict_overflow_p) == 0
	       && compare_values_warnv (vr0->max, vr1->max,
					strict_overflow_p) == 0)
	return boolean_false_node;

      /* Otherwise, they may or may not be different.  */
      else
	return NULL_TREE;
    }
  else if (comp == LT_EXPR || comp == LE_EXPR)
    {
      int tst;

      /* If VR0 is to the left of VR1, return true.  */
      tst = compare_values_warnv (vr0->max, vr1->min, strict_overflow_p);
      if ((comp == LT_EXPR && tst == -1)
	  || (comp == LE_EXPR && (tst == -1 || tst == 0)))
	{
	  if (overflow_infinity_range_p (vr0)
	      || overflow_infinity_range_p (vr1))
	    *strict_overflow_p = true;
	  return boolean_true_node;
	}

      /* If VR0 is to the right of VR1, return false.  */
      tst = compare_values_warnv (vr0->min, vr1->max, strict_overflow_p);
      if ((comp == LT_EXPR && (tst == 0 || tst == 1))
	  || (comp == LE_EXPR && tst == 1))
	{
	  if (overflow_infinity_range_p (vr0)
	      || overflow_infinity_range_p (vr1))
	    *strict_overflow_p = true;
	  return boolean_false_node;
	}

      /* Otherwise, we don't know.  */
      return NULL_TREE;
    }

  gcc_unreachable ();
}


/* Given a value range VR, a value VAL and a comparison code COMP, return
   BOOLEAN_TRUE_NODE if VR COMP VAL always returns true for all the
   values in VR.  Return BOOLEAN_FALSE_NODE if the comparison
   always returns false.  Return NULL_TREE if it is not always
   possible to determine the value of the comparison.  Also set
   *STRICT_OVERFLOW_P to indicate whether a range with an overflow
   infinity was used in the test.  */

static tree
compare_range_with_value (enum tree_code comp, value_range_t *vr, tree val,
			  bool *strict_overflow_p)
{
  if (vr->type == VR_VARYING || vr->type == VR_UNDEFINED)
    return NULL_TREE;

  /* Anti-ranges need to be handled separately.  */
  if (vr->type == VR_ANTI_RANGE)
    {
      /* For anti-ranges, the only predicates that we can compute at
	 compile time are equality and inequality.  */
      if (comp == GT_EXPR
	  || comp == GE_EXPR
	  || comp == LT_EXPR
	  || comp == LE_EXPR)
	return NULL_TREE;

      /* ~[VAL_1, VAL_2] OP VAL is known if VAL_1 <= VAL <= VAL_2.  */
      if (value_inside_range (val, vr->min, vr->max) == 1)
	return (comp == NE_EXPR) ? boolean_true_node : boolean_false_node;

      return NULL_TREE;
    }

  if (!usable_range_p (vr, strict_overflow_p))
    return NULL_TREE;

  if (comp == EQ_EXPR)
    {
      /* EQ_EXPR may only be computed if VR represents exactly
	 one value.  */
      if (compare_values_warnv (vr->min, vr->max, strict_overflow_p) == 0)
	{
	  int cmp = compare_values_warnv (vr->min, val, strict_overflow_p);
	  if (cmp == 0)
	    return boolean_true_node;
	  else if (cmp == -1 || cmp == 1 || cmp == 2)
	    return boolean_false_node;
	}
      else if (compare_values_warnv (val, vr->min, strict_overflow_p) == -1
	       || compare_values_warnv (vr->max, val, strict_overflow_p) == -1)
	return boolean_false_node;

      return NULL_TREE;
    }
  else if (comp == NE_EXPR)
    {
      /* If VAL is not inside VR, then they are always different.  */
      if (compare_values_warnv (vr->max, val, strict_overflow_p) == -1
	  || compare_values_warnv (vr->min, val, strict_overflow_p) == 1)
	return boolean_true_node;

      /* If VR represents exactly one value equal to VAL, then return
	 false.  */
      if (compare_values_warnv (vr->min, vr->max, strict_overflow_p) == 0
	  && compare_values_warnv (vr->min, val, strict_overflow_p) == 0)
	return boolean_false_node;

      /* Otherwise, they may or may not be different.  */
      return NULL_TREE;
    }
  else if (comp == LT_EXPR || comp == LE_EXPR)
    {
      int tst;

      /* If VR is to the left of VAL, return true.  */
      tst = compare_values_warnv (vr->max, val, strict_overflow_p);
      if ((comp == LT_EXPR && tst == -1)
	  || (comp == LE_EXPR && (tst == -1 || tst == 0)))
	{
	  if (overflow_infinity_range_p (vr))
	    *strict_overflow_p = true;
	  return boolean_true_node;
	}

      /* If VR is to the right of VAL, return false.  */
      tst = compare_values_warnv (vr->min, val, strict_overflow_p);
      if ((comp == LT_EXPR && (tst == 0 || tst == 1))
	  || (comp == LE_EXPR && tst == 1))
	{
	  if (overflow_infinity_range_p (vr))
	    *strict_overflow_p = true;
	  return boolean_false_node;
	}

      /* Otherwise, we don't know.  */
      return NULL_TREE;
    }
  else if (comp == GT_EXPR || comp == GE_EXPR)
    {
      int tst;

      /* If VR is to the right of VAL, return true.  */
      tst = compare_values_warnv (vr->min, val, strict_overflow_p);
      if ((comp == GT_EXPR && tst == 1)
	  || (comp == GE_EXPR && (tst == 0 || tst == 1)))
	{
	  if (overflow_infinity_range_p (vr))
	    *strict_overflow_p = true;
	  return boolean_true_node;
	}

      /* If VR is to the left of VAL, return false.  */
      tst = compare_values_warnv (vr->max, val, strict_overflow_p);
      if ((comp == GT_EXPR && (tst == -1 || tst == 0))
	  || (comp == GE_EXPR && tst == -1))
	{
	  if (overflow_infinity_range_p (vr))
	    *strict_overflow_p = true;
	  return boolean_false_node;
	}

      /* Otherwise, we don't know.  */
      return NULL_TREE;
    }

  gcc_unreachable ();
}

/* Given a COND_EXPR COND of the form 'V OP W', and an SSA name V,
   create a new SSA name N and return the assertion assignment
   'V = ASSERT_EXPR <V, V OP W>'.  */

static GIMPLE_type
build_assert_expr_for (tree cond, tree v)
{
  tree a;
  GIMPLE_type assertion;

  gcc_assert (TREE_CODE (v) == SSA_NAME
	      && COMPARISON_CLASS_P (cond));

  a = build2 (ASSERT_EXPR, TREE_TYPE (v), v, cond);
  assertion = gimple_build_assign (NULL_TREE, a);

  /* The new ASSERT_EXPR, creates a new SSA name that replaces the
     operand of the ASSERT_EXPR.  Create it so the new name and the old one
     are registered in the replacement table so that we can fix the SSA web
     after adding all the ASSERT_EXPRs.  */
  create_new_def_for (v, assertion, NULL);

  return assertion;
}


/* Return false if EXPR is a predicate expression involving floating
   point values.  */

static inline bool
fp_predicate (GIMPLE_type stmt)
{
  GIMPLE_CHECK (stmt, GIMPLE_COND);

  return FLOAT_TYPE_P (TREE_TYPE (gimple_cond_lhs (stmt)));
}


/* If the range of values taken by OP can be inferred after STMT executes,
   return the comparison code (COMP_CODE_P) and value (VAL_P) that
   describes the inferred range.  Return true if a range could be
   inferred.  */

static bool
infer_value_range (GIMPLE_type stmt, tree op, enum tree_code *comp_code_p, tree *val_p)
{
  *val_p = NULL_TREE;
  *comp_code_p = ERROR_MARK;

  /* Do not attempt to infer anything in names that flow through
     abnormal edges.  */
  if (SSA_NAME_OCCURS_IN_ABNORMAL_PHI (op))
    return false;

  /* Similarly, don't infer anything from statements that may throw
     exceptions.  */
  if (stmt_could_throw_p (stmt))
    return false;

  /* If STMT is the last statement of a basic block with no
     successors, there is no point inferring anything about any of its
     operands.  We would not be able to find a proper insertion point
     for the assertion, anyway.  */
  if (stmt_ends_bb_p (stmt) && EDGE_COUNT (gimple_bb (stmt)->succs) == 0)
    return false;

  /* We can only assume that a pointer dereference will yield
     non-NULL if -fdelete-null-pointer-checks is enabled.  */
  if (flag_delete_null_pointer_checks
      && POINTER_TYPE_P (TREE_TYPE (op))
      && gimple_code (stmt) != GIMPLE_ASM)
    {
      unsigned num_uses, num_loads, num_stores;

      count_uses_and_derefs (op, stmt, &num_uses, &num_loads, &num_stores);
      if (num_loads + num_stores > 0)
	{
	  *val_p = build_int_cst (TREE_TYPE (op), 0);
	  *comp_code_p = NE_EXPR;
	  return true;
	}
    }

  return false;
}


/* If NAME doesn't have an ASSERT_EXPR registered for asserting
   'EXPR COMP_CODE VAL' at a location that dominates block BB or
   E->DEST, then register this location as a possible insertion point
   for ASSERT_EXPR <NAME, EXPR COMP_CODE VAL>.

   BB, E and SI provide the exact insertion point for the new
   ASSERT_EXPR.  If BB is NULL, then the ASSERT_EXPR is to be inserted
   on edge E.  Otherwise, if E is NULL, the ASSERT_EXPR is inserted on
   BB.  If SI points to a COND_EXPR or a SWITCH_EXPR statement, then E
   must not be NULL.  */

static void
register_new_assert_for (tree name, tree expr,
			 enum tree_code comp_code,
			 tree val,
			 basic_block bb,
			 edge e,
			 gimple_stmt_iterator si)
{
  assert_locus_t n, loc, last_loc;
  basic_block dest_bb;

  gcc_checking_assert (bb == NULL || e == NULL);

  if (e == NULL)
    gcc_checking_assert (gimple_code (gsi_stmt (si)) != GIMPLE_COND
			 && gimple_code (gsi_stmt (si)) != GIMPLE_SWITCH);

  /* Never build an assert comparing against an integer constant with
     TREE_OVERFLOW set.  This confuses our undefined overflow warning
     machinery.  */
  if (TREE_CODE (val) == INTEGER_CST
      && TREE_OVERFLOW (val))
    val = build_int_cst_wide (TREE_TYPE (val),
			      TREE_INT_CST_LOW (val), TREE_INT_CST_HIGH (val));

  /* The new assertion A will be inserted at BB or E.  We need to
     determine if the new location is dominated by a previously
     registered location for A.  If we are doing an edge insertion,
     assume that A will be inserted at E->DEST.  Note that this is not
     necessarily true.

     If E is a critical edge, it will be split.  But even if E is
     split, the new block will dominate the same set of blocks that
     E->DEST dominates.

     The reverse, however, is not true, blocks dominated by E->DEST
     will not be dominated by the new block created to split E.  So,
     if the insertion location is on a critical edge, we will not use
     the new location to move another assertion previously registered
     at a block dominated by E->DEST.  */
  dest_bb = (bb) ? bb : e->dest;

  /* If NAME already has an ASSERT_EXPR registered for COMP_CODE and
     VAL at a block dominating DEST_BB, then we don't need to insert a new
     one.  Similarly, if the same assertion already exists at a block
     dominated by DEST_BB and the new location is not on a critical
     edge, then update the existing location for the assertion (i.e.,
     move the assertion up in the dominance tree).

     Note, this is implemented as a simple linked list because there
     should not be more than a handful of assertions registered per
     name.  If this becomes a performance problem, a table hashed by
     COMP_CODE and VAL could be implemented.  */
  loc = asserts_for[SSA_NAME_VERSION (name)];
  last_loc = loc;
  while (loc)
    {
      if (loc->comp_code == comp_code
	  && (loc->val == val
	      || operand_equal_p (loc->val, val, 0))
	  && (loc->expr == expr
	      || operand_equal_p (loc->expr, expr, 0)))
	{
	  /* If E is not a critical edge and DEST_BB
	     dominates the existing location for the assertion, move
	     the assertion up in the dominance tree by updating its
	     location information.  */
	  if ((e == NULL || !EDGE_CRITICAL_P (e))
	      && dominated_by_p (CDI_DOMINATORS, loc->bb, dest_bb))
	    {
	      loc->bb = dest_bb;
	      loc->e = e;
	      loc->si = si;
	      return;
	    }
	}

      /* Update the last node of the list and move to the next one.  */
      last_loc = loc;
      loc = loc->next;
    }

  /* If we didn't find an assertion already registered for
     NAME COMP_CODE VAL, add a new one at the end of the list of
     assertions associated with NAME.  */
  n = XNEW (struct assert_locus_d);
  n->bb = dest_bb;
  n->e = e;
  n->si = si;
  n->comp_code = comp_code;
  n->val = val;
  n->expr = expr;
  n->next = NULL;

  if (last_loc)
    last_loc->next = n;
  else
    asserts_for[SSA_NAME_VERSION (name)] = n;

  bitmap_set_bit (need_assert_for, SSA_NAME_VERSION (name));
}

/* (COND_OP0 COND_CODE COND_OP1) is a predicate which uses NAME.
   Extract a suitable test code and value and store them into *CODE_P and
   *VAL_P so the predicate is normalized to NAME *CODE_P *VAL_P.

   If no extraction was possible, return FALSE, otherwise return TRUE.

   If INVERT is true, then we invert the result stored into *CODE_P.  */

static bool
extract_code_and_val_from_cond_with_ops (tree name, enum tree_code cond_code,
					 tree cond_op0, tree cond_op1,
					 bool invert, enum tree_code *code_p,
					 tree *val_p)
{
  enum tree_code comp_code;
  tree val;

  /* Otherwise, we have a comparison of the form NAME COMP VAL
     or VAL COMP NAME.  */
  if (name == cond_op1)
    {
      /* If the predicate is of the form VAL COMP NAME, flip
	 COMP around because we need to register NAME as the
	 first operand in the predicate.  */
      comp_code = swap_tree_comparison (cond_code);
      val = cond_op0;
    }
  else
    {
      /* The comparison is of the form NAME COMP VAL, so the
	 comparison code remains unchanged.  */
      comp_code = cond_code;
      val = cond_op1;
    }

  /* Invert the comparison code as necessary.  */
  if (invert)
    comp_code = invert_tree_comparison (comp_code, 0);

  /* VRP does not handle float types.  */
  if (SCALAR_FLOAT_TYPE_P (TREE_TYPE (val)))
    return false;

  /* Do not register always-false predicates.
     FIXME:  this works around a limitation in fold() when dealing with
     enumerations.  Given 'enum { N1, N2 } x;', fold will not
     fold 'if (x > N2)' to 'if (0)'.  */
  if ((comp_code == GT_EXPR || comp_code == LT_EXPR)
      && INTEGRAL_TYPE_P (TREE_TYPE (val)))
    {
      tree min = TYPE_MIN_VALUE (TREE_TYPE (val));
      tree max = TYPE_MAX_VALUE (TREE_TYPE (val));

      if (comp_code == GT_EXPR
	  && (!max
	      || compare_values (val, max) == 0))
	return false;

      if (comp_code == LT_EXPR
	  && (!min
	      || compare_values (val, min) == 0))
	return false;
    }
  *code_p = comp_code;
  *val_p = val;
  return true;
}

/* Find out smallest RES where RES > VAL && (RES & MASK) == RES, if any
   (otherwise return VAL).  VAL and MASK must be zero-extended for
   precision PREC.  If SGNBIT is non-zero, first xor VAL with SGNBIT
   (to transform signed values into unsigned) and at the end xor
   SGNBIT back.  */

static double_int
masked_increment (double_int val, double_int mask, double_int sgnbit,
		  unsigned int prec)
{
  double_int bit = double_int_one, res;
  unsigned int i;

  val ^= sgnbit;
  for (i = 0; i < prec; i++, bit += bit)
    {
      res = mask;
      if ((res & bit).is_zero ())
	continue;
      res = bit - double_int_one;
      res = (val + bit).and_not (res);
      res &= mask;
      if (res.ugt (val))
	return res ^ sgnbit;
    }
  return val ^ sgnbit;
}

/* Try to register an edge assertion for SSA name NAME on edge E for
   the condition COND contributing to the conditional jump pointed to by BSI.
   Invert the condition COND if INVERT is true.
   Return true if an assertion for NAME could be registered.  */

static bool
register_edge_assert_for_2 (tree name, edge e, gimple_stmt_iterator bsi,
			    enum tree_code cond_code,
			    tree cond_op0, tree cond_op1, bool invert)
{
  tree val;
  enum tree_code comp_code;
  bool retval = false;

  if (!extract_code_and_val_from_cond_with_ops (name, cond_code,
						cond_op0,
						cond_op1,
						invert, &comp_code, &val))
    return false;

  /* Only register an ASSERT_EXPR if NAME was found in the sub-graph
     reachable from E.  */
  if (live_on_edge (e, name)
      && !has_single_use (name))
    {
      register_new_assert_for (name, name, comp_code, val, NULL, e, bsi);
      retval = true;
    }

  /* In the case of NAME <= CST and NAME being defined as
     NAME = (unsigned) NAME2 + CST2 we can assert NAME2 >= -CST2
     and NAME2 <= CST - CST2.  We can do the same for NAME > CST.
     This catches range and anti-range tests.  */
  if ((comp_code == LE_EXPR
       || comp_code == GT_EXPR)
      && TREE_CODE (val) == INTEGER_CST
      && TYPE_UNSIGNED (TREE_TYPE (val)))
    {
      GIMPLE_type def_stmt = SSA_NAME_DEF_STMT (name);
      tree cst2 = NULL_TREE, name2 = NULL_TREE, name3 = NULL_TREE;

      /* Extract CST2 from the (optional) addition.  */
      if (is_gimple_assign (def_stmt)
	  && gimple_assign_rhs_code (def_stmt) == PLUS_EXPR)
	{
	  name2 = gimple_assign_rhs1 (def_stmt);
	  cst2 = gimple_assign_rhs2 (def_stmt);
	  if (TREE_CODE (name2) == SSA_NAME
	      && TREE_CODE (cst2) == INTEGER_CST)
	    def_stmt = SSA_NAME_DEF_STMT (name2);
	}

      /* Extract NAME2 from the (optional) sign-changing cast.  */
      if (gimple_assign_cast_p (def_stmt))
	{
	  if (CONVERT_EXPR_CODE_P (gimple_assign_rhs_code (def_stmt))
	      && ! TYPE_UNSIGNED (TREE_TYPE (gimple_assign_rhs1 (def_stmt)))
	      && (TYPE_PRECISION (gimple_expr_type (def_stmt))
		  == TYPE_PRECISION (TREE_TYPE (gimple_assign_rhs1 (def_stmt)))))
	    name3 = gimple_assign_rhs1 (def_stmt);
	}

      /* If name3 is used later, create an ASSERT_EXPR for it.  */
      if (name3 != NULL_TREE
      	  && TREE_CODE (name3) == SSA_NAME
	  && (cst2 == NULL_TREE
	      || TREE_CODE (cst2) == INTEGER_CST)
	  && INTEGRAL_TYPE_P (TREE_TYPE (name3))
	  && live_on_edge (e, name3)
	  && !has_single_use (name3))
	{
	  tree tmp;

	  /* Build an expression for the range test.  */
	  tmp = build1 (NOP_EXPR, TREE_TYPE (name), name3);
	  if (cst2 != NULL_TREE)
	    tmp = build2 (PLUS_EXPR, TREE_TYPE (name), tmp, cst2);

	  if (dump_file)
	    {
	      fprintf (dump_file, "Adding assert for ");
	      print_generic_expr (dump_file, name3, 0);
	      fprintf (dump_file, " from ");
	      print_generic_expr (dump_file, tmp, 0);
	      fprintf (dump_file, "\n");
	    }

	  register_new_assert_for (name3, tmp, comp_code, val, NULL, e, bsi);

	  retval = true;
	}

      /* If name2 is used later, create an ASSERT_EXPR for it.  */
      if (name2 != NULL_TREE
      	  && TREE_CODE (name2) == SSA_NAME
	  && TREE_CODE (cst2) == INTEGER_CST
	  && INTEGRAL_TYPE_P (TREE_TYPE (name2))
	  && live_on_edge (e, name2)
	  && !has_single_use (name2))
	{
	  tree tmp;

	  /* Build an expression for the range test.  */
	  tmp = name2;
	  if (TREE_TYPE (name) != TREE_TYPE (name2))
	    tmp = build1 (NOP_EXPR, TREE_TYPE (name), tmp);
	  if (cst2 != NULL_TREE)
	    tmp = build2 (PLUS_EXPR, TREE_TYPE (name), tmp, cst2);

	  if (dump_file)
	    {
	      fprintf (dump_file, "Adding assert for ");
	      print_generic_expr (dump_file, name2, 0);
	      fprintf (dump_file, " from ");
	      print_generic_expr (dump_file, tmp, 0);
	      fprintf (dump_file, "\n");
	    }

	  register_new_assert_for (name2, tmp, comp_code, val, NULL, e, bsi);

	  retval = true;
	}
    }

  if (TREE_CODE_CLASS (comp_code) == tcc_comparison
      && TREE_CODE (val) == INTEGER_CST)
    {
      GIMPLE_type def_stmt = SSA_NAME_DEF_STMT (name);
      tree name2 = NULL_TREE, names[2], cst2 = NULL_TREE;
      tree val2 = NULL_TREE;
      double_int mask = double_int_zero;
      unsigned int prec = TYPE_PRECISION (TREE_TYPE (val));
      unsigned int nprec = prec;
      enum tree_code rhs_code = ERROR_MARK;

      if (is_gimple_assign (def_stmt))
	rhs_code = gimple_assign_rhs_code (def_stmt);

      /* Add asserts for NAME cmp CST and NAME being defined
	 as NAME = (int) NAME2.  */
      if (!TYPE_UNSIGNED (TREE_TYPE (val))
	  && (comp_code == LE_EXPR || comp_code == LT_EXPR
	      || comp_code == GT_EXPR || comp_code == GE_EXPR)
	  && gimple_assign_cast_p (def_stmt))
	{
	  name2 = gimple_assign_rhs1 (def_stmt);
	  if (CONVERT_EXPR_CODE_P (rhs_code)
	      && INTEGRAL_TYPE_P (TREE_TYPE (name2))
	      && TYPE_UNSIGNED (TREE_TYPE (name2))
	      && prec == TYPE_PRECISION (TREE_TYPE (name2))
	      && (comp_code == LE_EXPR || comp_code == GT_EXPR
		  || !tree_int_cst_equal (val,
					  TYPE_MIN_VALUE (TREE_TYPE (val))))
	      && live_on_edge (e, name2)
	      && !has_single_use (name2))
	    {
	      tree tmp, cst;
	      enum tree_code new_comp_code = comp_code;

	      cst = fold_convert (TREE_TYPE (name2),
				  TYPE_MIN_VALUE (TREE_TYPE (val)));
	      /* Build an expression for the range test.  */
	      tmp = build2 (PLUS_EXPR, TREE_TYPE (name2), name2, cst);
	      cst = fold_build2 (PLUS_EXPR, TREE_TYPE (name2), cst,
				 fold_convert (TREE_TYPE (name2), val));
	      if (comp_code == LT_EXPR || comp_code == GE_EXPR)
		{
		  new_comp_code = comp_code == LT_EXPR ? LE_EXPR : GT_EXPR;
		  cst = fold_build2 (MINUS_EXPR, TREE_TYPE (name2), cst,
				     build_int_cst (TREE_TYPE (name2), 1));
		}

	      if (dump_file)
		{
		  fprintf (dump_file, "Adding assert for ");
		  print_generic_expr (dump_file, name2, 0);
		  fprintf (dump_file, " from ");
		  print_generic_expr (dump_file, tmp, 0);
		  fprintf (dump_file, "\n");
		}

	      register_new_assert_for (name2, tmp, new_comp_code, cst, NULL,
				       e, bsi);

	      retval = true;
	    }
	}

      /* Add asserts for NAME cmp CST and NAME being defined as
	 NAME = NAME2 >> CST2.

	 Extract CST2 from the right shift.  */
      if (rhs_code == RSHIFT_EXPR)
	{
	  name2 = gimple_assign_rhs1 (def_stmt);
	  cst2 = gimple_assign_rhs2 (def_stmt);
	  if (TREE_CODE (name2) == SSA_NAME
	      && host_integerp (cst2, 1)
	      && INTEGRAL_TYPE_P (TREE_TYPE (name2))
	      && IN_RANGE (tree_low_cst (cst2, 1), 1, prec - 1)
	      && prec <= HOST_BITS_PER_DOUBLE_INT
	      && prec == GET_MODE_PRECISION (TYPE_MODE (TREE_TYPE (val)))
	      && live_on_edge (e, name2)
	      && !has_single_use (name2))
	    {
	      mask = double_int::mask (tree_low_cst (cst2, 1));
	      val2 = fold_binary (LSHIFT_EXPR, TREE_TYPE (val), val, cst2);
	    }
	}
      if (val2 != NULL_TREE
	  && TREE_CODE (val2) == INTEGER_CST
	  && simple_cst_equal (fold_build2 (RSHIFT_EXPR,
					    TREE_TYPE (val),
					    val2, cst2), val))
	{
	  enum tree_code new_comp_code = comp_code;
	  tree tmp, new_val;

	  tmp = name2;
	  if (comp_code == EQ_EXPR || comp_code == NE_EXPR)
	    {
	      if (!TYPE_UNSIGNED (TREE_TYPE (val)))
		{
		  tree type = build_nonstandard_integer_type (prec, 1);
		  tmp = build1 (NOP_EXPR, type, name2);
		  val2 = fold_convert (type, val2);
		}
	      tmp = fold_build2 (MINUS_EXPR, TREE_TYPE (tmp), tmp, val2);
	      new_val = double_int_to_tree (TREE_TYPE (tmp), mask);
	      new_comp_code = comp_code == EQ_EXPR ? LE_EXPR : GT_EXPR;
	    }
	  else if (comp_code == LT_EXPR || comp_code == GE_EXPR)
	    new_val = val2;
	  else
	    {
	      double_int maxval
		= double_int::max_value (prec, TYPE_UNSIGNED (TREE_TYPE (val)));
	      mask |= tree_to_double_int (val2);
	      if (mask == maxval)
		new_val = NULL_TREE;
	      else
		new_val = double_int_to_tree (TREE_TYPE (val2), mask);
	    }

	  if (new_val)
	    {
	      if (dump_file)
		{
		  fprintf (dump_file, "Adding assert for ");
		  print_generic_expr (dump_file, name2, 0);
		  fprintf (dump_file, " from ");
		  print_generic_expr (dump_file, tmp, 0);
		  fprintf (dump_file, "\n");
		}

	      register_new_assert_for (name2, tmp, new_comp_code, new_val,
				       NULL, e, bsi);
	      retval = true;
	    }
	}

      /* Add asserts for NAME cmp CST and NAME being defined as
	 NAME = NAME2 & CST2.

	 Extract CST2 from the and.

	 Also handle
	 NAME = (unsigned) NAME2;
	 casts where NAME's type is unsigned and has smaller precision
	 than NAME2's type as if it was NAME = NAME2 & MASK.  */
      names[0] = NULL_TREE;
      names[1] = NULL_TREE;
      cst2 = NULL_TREE;
      if (rhs_code == BIT_AND_EXPR
	  || (CONVERT_EXPR_CODE_P (rhs_code)
	      && TREE_CODE (TREE_TYPE (val)) == INTEGER_TYPE
	      && TYPE_UNSIGNED (TREE_TYPE (val))
	      && TYPE_PRECISION (TREE_TYPE (gimple_assign_rhs1 (def_stmt)))
		 > prec
	      && !retval))
	{
	  name2 = gimple_assign_rhs1 (def_stmt);
	  if (rhs_code == BIT_AND_EXPR)
	    cst2 = gimple_assign_rhs2 (def_stmt);
	  else
	    {
	      cst2 = TYPE_MAX_VALUE (TREE_TYPE (val));
	      nprec = TYPE_PRECISION (TREE_TYPE (name2));
	    }
	  if (TREE_CODE (name2) == SSA_NAME
	      && INTEGRAL_TYPE_P (TREE_TYPE (name2))
	      && TREE_CODE (cst2) == INTEGER_CST
	      && !integer_zerop (cst2)
	      && nprec <= HOST_BITS_PER_DOUBLE_INT
	      && (nprec > 1
		  || TYPE_UNSIGNED (TREE_TYPE (val))))
	    {
         GIMPLE_type def_stmt2 = SSA_NAME_DEF_STMT (name2);
	      if (gimple_assign_cast_p (def_stmt2))
		{
		  names[1] = gimple_assign_rhs1 (def_stmt2);
		  if (!CONVERT_EXPR_CODE_P (gimple_assign_rhs_code (def_stmt2))
		      || !INTEGRAL_TYPE_P (TREE_TYPE (names[1]))
		      || (TYPE_PRECISION (TREE_TYPE (name2))
			  != TYPE_PRECISION (TREE_TYPE (names[1])))
		      || !live_on_edge (e, names[1])
		      || has_single_use (names[1]))
		    names[1] = NULL_TREE;
		}
	      if (live_on_edge (e, name2)
		  && !has_single_use (name2))
		names[0] = name2;
	    }
	}
      if (names[0] || names[1])
	{
	  double_int minv, maxv = double_int_zero, valv, cst2v;
	  double_int tem, sgnbit;
	  bool valid_p = false, valn = false, cst2n = false;
	  enum tree_code ccode = comp_code;

	  valv = tree_to_double_int (val).zext (nprec);
	  cst2v = tree_to_double_int (cst2).zext (nprec);
	  if (!TYPE_UNSIGNED (TREE_TYPE (val)))
	    {
	      valn = valv.sext (nprec).is_negative ();
	      cst2n = cst2v.sext (nprec).is_negative ();
	    }
	  /* If CST2 doesn't have most significant bit set,
	     but VAL is negative, we have comparison like
	     if ((x & 0x123) > -4) (always true).  Just give up.  */
	  if (!cst2n && valn)
	    ccode = ERROR_MARK;
	  if (cst2n)
	    sgnbit = double_int_one.llshift (nprec - 1, nprec).zext (nprec);
	  else
	    sgnbit = double_int_zero;
	  minv = valv & cst2v;
	  switch (ccode)
	    {
	    case EQ_EXPR:
	      /* Minimum unsigned value for equality is VAL & CST2
		 (should be equal to VAL, otherwise we probably should
		 have folded the comparison into false) and
		 maximum unsigned value is VAL | ~CST2.  */
	      maxv = valv | ~cst2v;
	      maxv = maxv.zext (nprec);
	      valid_p = true;
	      break;
	    case NE_EXPR:
	      tem = valv | ~cst2v;
	      tem = tem.zext (nprec);
	      /* If VAL is 0, handle (X & CST2) != 0 as (X & CST2) > 0U.  */
	      if (valv.is_zero ())
		{
		  cst2n = false;
		  sgnbit = double_int_zero;
		  goto gt_expr;
		}
	      /* If (VAL | ~CST2) is all ones, handle it as
		 (X & CST2) < VAL.  */
	      if (tem == double_int::mask (nprec))
		{
		  cst2n = false;
		  valn = false;
		  sgnbit = double_int_zero;
		  goto lt_expr;
		}
	      if (!cst2n
		  && cst2v.sext (nprec).is_negative ())
		sgnbit
		  = double_int_one.llshift (nprec - 1, nprec).zext (nprec);
	      if (!sgnbit.is_zero ())
		{
		  if (valv == sgnbit)
		    {
		      cst2n = true;
		      valn = true;
		      goto gt_expr;
		    }
		  if (tem == double_int::mask (nprec - 1))
		    {
		      cst2n = true;
		      goto lt_expr;
		    }
		  if (!cst2n)
		    sgnbit = double_int_zero;
		}
	      break;
	    case GE_EXPR:
	      /* Minimum unsigned value for >= if (VAL & CST2) == VAL
		 is VAL and maximum unsigned value is ~0.  For signed
		 comparison, if CST2 doesn't have most significant bit
		 set, handle it similarly.  If CST2 has MSB set,
		 the minimum is the same, and maximum is ~0U/2.  */
	      if (minv != valv)
		{
		  /* If (VAL & CST2) != VAL, X & CST2 can't be equal to
		     VAL.  */
		  minv = masked_increment (valv, cst2v, sgnbit, nprec);
		  if (minv == valv)
		    break;
		}
	      maxv = double_int::mask (nprec - (cst2n ? 1 : 0));
	      valid_p = true;
	      break;
	    case GT_EXPR:
	    gt_expr:
	      /* Find out smallest MINV where MINV > VAL
		 && (MINV & CST2) == MINV, if any.  If VAL is signed and
		 CST2 has MSB set, compute it biased by 1 << (nprec - 1).  */
	      minv = masked_increment (valv, cst2v, sgnbit, nprec);
	      if (minv == valv)
		break;
	      maxv = double_int::mask (nprec - (cst2n ? 1 : 0));
	      valid_p = true;
	      break;
	    case LE_EXPR:
	      /* Minimum unsigned value for <= is 0 and maximum
		 unsigned value is VAL | ~CST2 if (VAL & CST2) == VAL.
		 Otherwise, find smallest VAL2 where VAL2 > VAL
		 && (VAL2 & CST2) == VAL2 and use (VAL2 - 1) | ~CST2
		 as maximum.
		 For signed comparison, if CST2 doesn't have most
		 significant bit set, handle it similarly.  If CST2 has
		 MSB set, the maximum is the same and minimum is INT_MIN.  */
	      if (minv == valv)
		maxv = valv;
	      else
		{
		  maxv = masked_increment (valv, cst2v, sgnbit, nprec);
		  if (maxv == valv)
		    break;
		  maxv -= double_int_one;
		}
	      maxv |= ~cst2v;
	      maxv = maxv.zext (nprec);
	      minv = sgnbit;
	      valid_p = true;
	      break;
	    case LT_EXPR:
	    lt_expr:
	      /* Minimum unsigned value for < is 0 and maximum
		 unsigned value is (VAL-1) | ~CST2 if (VAL & CST2) == VAL.
		 Otherwise, find smallest VAL2 where VAL2 > VAL
		 && (VAL2 & CST2) == VAL2 and use (VAL2 - 1) | ~CST2
		 as maximum.
		 For signed comparison, if CST2 doesn't have most
		 significant bit set, handle it similarly.  If CST2 has
		 MSB set, the maximum is the same and minimum is INT_MIN.  */
	      if (minv == valv)
		{
		  if (valv == sgnbit)
		    break;
		  maxv = valv;
		}
	      else
		{
		  maxv = masked_increment (valv, cst2v, sgnbit, nprec);
		  if (maxv == valv)
		    break;
		}
	      maxv -= double_int_one;
	      maxv |= ~cst2v;
	      maxv = maxv.zext (nprec);
	      minv = sgnbit;
	      valid_p = true;
	      break;
	    default:
	      break;
	    }
	  if (valid_p
	      && (maxv - minv).zext (nprec) != double_int::mask (nprec))
	    {
	      tree tmp, new_val, type;
	      int i;

	      for (i = 0; i < 2; i++)
		if (names[i])
		  {
		    double_int maxv2 = maxv;
		    tmp = names[i];
		    type = TREE_TYPE (names[i]);
		    if (!TYPE_UNSIGNED (type))
		      {
			type = build_nonstandard_integer_type (nprec, 1);
			tmp = build1 (NOP_EXPR, type, names[i]);
		      }
		    if (!minv.is_zero ())
		      {
			tmp = build2 (PLUS_EXPR, type, tmp,
				      double_int_to_tree (type, -minv));
			maxv2 = maxv - minv;
		      }
		    new_val = double_int_to_tree (type, maxv2);

		    if (dump_file)
		      {
			fprintf (dump_file, "Adding assert for ");
			print_generic_expr (dump_file, names[i], 0);
			fprintf (dump_file, " from ");
			print_generic_expr (dump_file, tmp, 0);
			fprintf (dump_file, "\n");
		      }

		    register_new_assert_for (names[i], tmp, LE_EXPR,
					     new_val, NULL, e, bsi);
		    retval = true;
		  }
	    }
	}
    }

  return retval;
}

/* OP is an operand of a truth value expression which is known to have
   a particular value.  Register any asserts for OP and for any
   operands in OP's defining statement.

   If CODE is EQ_EXPR, then we want to register OP is zero (false),
   if CODE is NE_EXPR, then we want to register OP is nonzero (true).   */

static bool
register_edge_assert_for_1 (tree op, enum tree_code code,
			    edge e, gimple_stmt_iterator bsi)
{
  bool retval = false;
  GIMPLE_type op_def;
  tree val;
  enum tree_code rhs_code;

  /* We only care about SSA_NAMEs.  */
  if (TREE_CODE (op) != SSA_NAME)
    return false;

  /* We know that OP will have a zero or nonzero value.  If OP is used
     more than once go ahead and register an assert for OP.

     The FOUND_IN_SUBGRAPH support is not helpful in this situation as
     it will always be set for OP (because OP is used in a COND_EXPR in
     the subgraph).  */
  if (!has_single_use (op))
    {
      val = build_int_cst (TREE_TYPE (op), 0);
      register_new_assert_for (op, op, code, val, NULL, e, bsi);
      retval = true;
    }

  /* Now look at how OP is set.  If it's set from a comparison,
     a truth operation or some bit operations, then we may be able
     to register information about the operands of that assignment.  */
  op_def = SSA_NAME_DEF_STMT (op);
  if (gimple_code (op_def) != GIMPLE_ASSIGN)
    return retval;

  rhs_code = gimple_assign_rhs_code (op_def);

  if (TREE_CODE_CLASS (rhs_code) == tcc_comparison)
    {
      bool invert = (code == EQ_EXPR ? true : false);
      tree op0 = gimple_assign_rhs1 (op_def);
      tree op1 = gimple_assign_rhs2 (op_def);

      if (TREE_CODE (op0) == SSA_NAME)
        retval |= register_edge_assert_for_2 (op0, e, bsi, rhs_code, op0, op1,
					      invert);
      if (TREE_CODE (op1) == SSA_NAME)
        retval |= register_edge_assert_for_2 (op1, e, bsi, rhs_code, op0, op1,
					      invert);
    }
  else if ((code == NE_EXPR
	    && gimple_assign_rhs_code (op_def) == BIT_AND_EXPR)
	   || (code == EQ_EXPR
	       && gimple_assign_rhs_code (op_def) == BIT_IOR_EXPR))
    {
      /* Recurse on each operand.  */
      retval |= register_edge_assert_for_1 (gimple_assign_rhs1 (op_def),
					    code, e, bsi);
      retval |= register_edge_assert_for_1 (gimple_assign_rhs2 (op_def),
					    code, e, bsi);
    }
  else if (gimple_assign_rhs_code (op_def) == BIT_NOT_EXPR
	   && TYPE_PRECISION (TREE_TYPE (gimple_assign_lhs (op_def))) == 1)
    {
      /* Recurse, flipping CODE.  */
      code = invert_tree_comparison (code, false);
      retval |= register_edge_assert_for_1 (gimple_assign_rhs1 (op_def),
					    code, e, bsi);
    }
  else if (gimple_assign_rhs_code (op_def) == SSA_NAME)
    {
      /* Recurse through the copy.  */
      retval |= register_edge_assert_for_1 (gimple_assign_rhs1 (op_def),
					    code, e, bsi);
    }
  else if (CONVERT_EXPR_CODE_P (gimple_assign_rhs_code (op_def)))
    {
      /* Recurse through the type conversion.  */
      retval |= register_edge_assert_for_1 (gimple_assign_rhs1 (op_def),
					    code, e, bsi);
    }

  return retval;
}

/* Try to register an edge assertion for SSA name NAME on edge E for
   the condition COND contributing to the conditional jump pointed to by SI.
   Return true if an assertion for NAME could be registered.  */

static bool
register_edge_assert_for (tree name, edge e, gimple_stmt_iterator si,
			  enum tree_code cond_code, tree cond_op0,
			  tree cond_op1)
{
  tree val;
  enum tree_code comp_code;
  bool retval = false;
  bool is_else_edge = (e->flags & EDGE_FALSE_VALUE) != 0;

  /* Do not attempt to infer anything in names that flow through
     abnormal edges.  */
  if (SSA_NAME_OCCURS_IN_ABNORMAL_PHI (name))
    return false;

  if (!extract_code_and_val_from_cond_with_ops (name, cond_code,
						cond_op0, cond_op1,
						is_else_edge,
						&comp_code, &val))
    return false;

  /* Register ASSERT_EXPRs for name.  */
  retval |= register_edge_assert_for_2 (name, e, si, cond_code, cond_op0,
					cond_op1, is_else_edge);


  /* If COND is effectively an equality test of an SSA_NAME against
     the value zero or one, then we may be able to assert values
     for SSA_NAMEs which flow into COND.  */

  /* In the case of NAME == 1 or NAME != 0, for BIT_AND_EXPR defining
     statement of NAME we can assert both operands of the BIT_AND_EXPR
     have nonzero value.  */
  if (((comp_code == EQ_EXPR && integer_onep (val))
       || (comp_code == NE_EXPR && integer_zerop (val))))
    {
      GIMPLE_type def_stmt = SSA_NAME_DEF_STMT (name);

      if (is_gimple_assign (def_stmt)
	  && gimple_assign_rhs_code (def_stmt) == BIT_AND_EXPR)
	{
	  tree op0 = gimple_assign_rhs1 (def_stmt);
	  tree op1 = gimple_assign_rhs2 (def_stmt);
	  retval |= register_edge_assert_for_1 (op0, NE_EXPR, e, si);
	  retval |= register_edge_assert_for_1 (op1, NE_EXPR, e, si);
	}
    }

  /* In the case of NAME == 0 or NAME != 1, for BIT_IOR_EXPR defining
     statement of NAME we can assert both operands of the BIT_IOR_EXPR
     have zero value.  */
  if (((comp_code == EQ_EXPR && integer_zerop (val))
       || (comp_code == NE_EXPR && integer_onep (val))))
    {
      GIMPLE_type def_stmt = SSA_NAME_DEF_STMT (name);

      /* For BIT_IOR_EXPR only if NAME == 0 both operands have
	 necessarily zero value, or if type-precision is one.  */
      if (is_gimple_assign (def_stmt)
	  && (gimple_assign_rhs_code (def_stmt) == BIT_IOR_EXPR
	      && (TYPE_PRECISION (TREE_TYPE (name)) == 1
	          || comp_code == EQ_EXPR)))
	{
	  tree op0 = gimple_assign_rhs1 (def_stmt);
	  tree op1 = gimple_assign_rhs2 (def_stmt);
	  retval |= register_edge_assert_for_1 (op0, EQ_EXPR, e, si);
	  retval |= register_edge_assert_for_1 (op1, EQ_EXPR, e, si);
	}
    }

  return retval;
}


/* Determine whether the outgoing edges of BB should receive an
   ASSERT_EXPR for each of the operands of BB's LAST statement.
   The last statement of BB must be a COND_EXPR.

   If any of the sub-graphs rooted at BB have an interesting use of
   the predicate operands, an assert location node is added to the
   list of assertions for the corresponding operands.  */

static bool
find_conditional_asserts (basic_block bb, GIMPLE_type last)
{
  bool need_assert;
  gimple_stmt_iterator bsi;
  tree op;
  edge_iterator ei;
  edge e;
  ssa_op_iter iter;

  need_assert = false;
  bsi = gsi_for_stmt (last);

  /* Look for uses of the operands in each of the sub-graphs
     rooted at BB.  We need to check each of the outgoing edges
     separately, so that we know what kind of ASSERT_EXPR to
     insert.  */
  FOR_EACH_EDGE (e, ei, bb->succs)
    {
      if (e->dest == bb)
	continue;

      /* Register the necessary assertions for each operand in the
	 conditional predicate.  */
      FOR_EACH_SSA_TREE_OPERAND (op, last, iter, SSA_OP_USE)
	{
	  need_assert |= register_edge_assert_for (op, e, bsi,
						   gimple_cond_code (last),
						   gimple_cond_lhs (last),
						   gimple_cond_rhs (last));
	}
    }

  return need_assert;
}

struct case_info
{
  tree expr;
  basic_block bb;
};

/* Compare two case labels sorting first by the destination bb index
   and then by the case value.  */

static int
compare_case_labels (const void *p1, const void *p2)
{
  const struct case_info *ci1 = (const struct case_info *) p1;
  const struct case_info *ci2 = (const struct case_info *) p2;
  int idx1 = ci1->bb->index;
  int idx2 = ci2->bb->index;

  if (idx1 < idx2)
    return -1;
  else if (idx1 == idx2)
    {
      /* Make sure the default label is first in a group.  */
      if (!CASE_LOW (ci1->expr))
	return -1;
      else if (!CASE_LOW (ci2->expr))
	return 1;
      else
	return tree_int_cst_compare (CASE_LOW (ci1->expr),
				     CASE_LOW (ci2->expr));
    }
  else
    return 1;
}

/* Determine whether the outgoing edges of BB should receive an
   ASSERT_EXPR for each of the operands of BB's LAST statement.
   The last statement of BB must be a SWITCH_EXPR.

   If any of the sub-graphs rooted at BB have an interesting use of
   the predicate operands, an assert location node is added to the
   list of assertions for the corresponding operands.  */

static bool
find_switch_asserts (basic_block bb, GIMPLE_type last)
{
  bool need_assert;
  gimple_stmt_iterator bsi;
  tree op;
  edge e;
  struct case_info *ci;
  size_t n = gimple_switch_num_labels (last);
#if GCC_VERSION >= 4000
  unsigned int idx;
#else
  /* Work around GCC 3.4 bug (PR 37086).  */
  volatile unsigned int idx;
#endif

  need_assert = false;
  bsi = gsi_for_stmt (last);
  op = gimple_switch_index (last);
  if (TREE_CODE (op) != SSA_NAME)
    return false;

  /* Build a vector of case labels sorted by destination label.  */
  ci = XNEWVEC (struct case_info, n);
  for (idx = 0; idx < n; ++idx)
    {
      ci[idx].expr = gimple_switch_label (last, idx);
      ci[idx].bb = label_to_block (CASE_LABEL (ci[idx].expr));
    }
  qsort (ci, n, sizeof (struct case_info), compare_case_labels);

  for (idx = 0; idx < n; ++idx)
    {
      tree min, max;
      tree cl = ci[idx].expr;
      basic_block cbb = ci[idx].bb;

      min = CASE_LOW (cl);
      max = CASE_HIGH (cl);

      /* If there are multiple case labels with the same destination
	 we need to combine them to a single value range for the edge.  */
      if (idx + 1 < n && cbb == ci[idx + 1].bb)
	{
	  /* Skip labels until the last of the group.  */
	  do {
	    ++idx;
	  } while (idx < n && cbb == ci[idx].bb);
	  --idx;

	  /* Pick up the maximum of the case label range.  */
	  if (CASE_HIGH (ci[idx].expr))
	    max = CASE_HIGH (ci[idx].expr);
	  else
	    max = CASE_LOW (ci[idx].expr);
	}

      /* Nothing to do if the range includes the default label until we
	 can register anti-ranges.  */
      if (min == NULL_TREE)
	continue;

      /* Find the edge to register the assert expr on.  */
      e = find_edge (bb, cbb);

      /* Register the necessary assertions for the operand in the
	 SWITCH_EXPR.  */
      need_assert |= register_edge_assert_for (op, e, bsi,
					       max ? GE_EXPR : EQ_EXPR,
					       op,
					       fold_convert (TREE_TYPE (op),
							     min));
      if (max)
	{
	  need_assert |= register_edge_assert_for (op, e, bsi, LE_EXPR,
						   op,
						   fold_convert (TREE_TYPE (op),
								 max));
	}
    }

  XDELETEVEC (ci);
  return need_assert;
}


/* Traverse all the statements in block BB looking for statements that
   may generate useful assertions for the SSA names in their operand.
   If a statement produces a useful assertion A for name N_i, then the
   list of assertions already generated for N_i is scanned to
   determine if A is actually needed.

   If N_i already had the assertion A at a location dominating the
   current location, then nothing needs to be done.  Otherwise, the
   new location for A is recorded instead.

   1- For every statement S in BB, all the variables used by S are
      added to bitmap FOUND_IN_SUBGRAPH.

   2- If statement S uses an operand N in a way that exposes a known
      value range for N, then if N was not already generated by an
      ASSERT_EXPR, create a new assert location for N.  For instance,
      if N is a pointer and the statement dereferences it, we can
      assume that N is not NULL.

   3- COND_EXPRs are a special case of #2.  We can derive range
      information from the predicate but need to insert different
      ASSERT_EXPRs for each of the sub-graphs rooted at the
      conditional block.  If the last statement of BB is a conditional
      expression of the form 'X op Y', then

      a) Remove X and Y from the set FOUND_IN_SUBGRAPH.

      b) If the conditional is the only entry point to the sub-graph
	 corresponding to the THEN_CLAUSE, recurse into it.  On
	 return, if X and/or Y are marked in FOUND_IN_SUBGRAPH, then
	 an ASSERT_EXPR is added for the corresponding variable.

      c) Repeat step (b) on the ELSE_CLAUSE.

      d) Mark X and Y in FOUND_IN_SUBGRAPH.

      For instance,

	    if (a == 9)
	      b = a;
	    else
	      b = c + 1;

      In this case, an assertion on the THEN clause is useful to
      determine that 'a' is always 9 on that edge.  However, an assertion
      on the ELSE clause would be unnecessary.

   4- If BB does not end in a conditional expression, then we recurse
      into BB's dominator children.

   At the end of the recursive traversal, every SSA name will have a
   list of locations where ASSERT_EXPRs should be added.  When a new
   location for name N is found, it is registered by calling
   register_new_assert_for.  That function keeps track of all the
   registered assertions to prevent adding unnecessary assertions.
   For instance, if a pointer P_4 is dereferenced more than once in a
   dominator tree, only the location dominating all the dereference of
   P_4 will receive an ASSERT_EXPR.

   If this function returns true, then it means that there are names
   for which we need to generate ASSERT_EXPRs.  Those assertions are
   inserted by process_assert_insertions.  */

static bool
find_assert_locations_1 (basic_block bb, sbitmap live)
{
  gimple_stmt_iterator si;
  GIMPLE_type last;
  bool need_assert;

  need_assert = false;
  last = last_stmt (bb);

  /* If BB's last statement is a conditional statement involving integer
     operands, determine if we need to add ASSERT_EXPRs.  */
  if (last
      && gimple_code (last) == GIMPLE_COND
      && !fp_predicate (last)
      && !ZERO_SSA_OPERANDS (last, SSA_OP_USE))
    need_assert |= find_conditional_asserts (bb, last);

  /* If BB's last statement is a switch statement involving integer
     operands, determine if we need to add ASSERT_EXPRs.  */
  if (last
      && gimple_code (last) == GIMPLE_SWITCH
      && !ZERO_SSA_OPERANDS (last, SSA_OP_USE))
    need_assert |= find_switch_asserts (bb, last);

  /* Traverse all the statements in BB marking used names and looking
     for statements that may infer assertions for their used operands.  */
  for (si = gsi_last_bb (bb); !gsi_end_p (si); gsi_prev (&si))
    {
      GIMPLE_type stmt;
      tree op;
      ssa_op_iter i;

      stmt = gsi_stmt (si);

      if (is_gimple_debug (stmt))
	continue;

      /* See if we can derive an assertion for any of STMT's operands.  */
      FOR_EACH_SSA_TREE_OPERAND (op, stmt, i, SSA_OP_USE)
	{
	  tree value;
	  enum tree_code comp_code;

	  /* If op is not live beyond this stmt, do not bother to insert
	     asserts for it.  */
	  if (!bitmap_bit_p (live, SSA_NAME_VERSION (op)))
	    continue;

	  /* If OP is used in such a way that we can infer a value
	     range for it, and we don't find a previous assertion for
	     it, create a new assertion location node for OP.  */
	  if (infer_value_range (stmt, op, &comp_code, &value))
	    {
	      /* If we are able to infer a nonzero value range for OP,
		 then walk backwards through the use-def chain to see if OP
		 was set via a typecast.

		 If so, then we can also infer a nonzero value range
		 for the operand of the NOP_EXPR.  */
	      if (comp_code == NE_EXPR && integer_zerop (value))
		{
		  tree t = op;
        GIMPLE_type def_stmt = SSA_NAME_DEF_STMT (t);

		  while (is_gimple_assign (def_stmt)
			 && gimple_assign_rhs_code (def_stmt)  == NOP_EXPR
			 && TREE_CODE
			     (gimple_assign_rhs1 (def_stmt)) == SSA_NAME
			 && POINTER_TYPE_P
			     (TREE_TYPE (gimple_assign_rhs1 (def_stmt))))
		    {
		      t = gimple_assign_rhs1 (def_stmt);
		      def_stmt = SSA_NAME_DEF_STMT (t);

		      /* Note we want to register the assert for the
			 operand of the NOP_EXPR after SI, not after the
			 conversion.  */
		      if (! has_single_use (t))
			{
			  register_new_assert_for (t, t, comp_code, value,
						   bb, NULL, si);
			  need_assert = true;
			}
		    }
		}

	      register_new_assert_for (op, op, comp_code, value, bb, NULL, si);
	      need_assert = true;
	    }
	}

      /* Update live.  */
      FOR_EACH_SSA_TREE_OPERAND (op, stmt, i, SSA_OP_USE)
	bitmap_set_bit (live, SSA_NAME_VERSION (op));
      FOR_EACH_SSA_TREE_OPERAND (op, stmt, i, SSA_OP_DEF)
	bitmap_clear_bit (live, SSA_NAME_VERSION (op));
    }

  /* Traverse all PHI nodes in BB, updating live.  */
  for (si = gsi_start_phis (bb); !gsi_end_p(si); gsi_next (&si))
    {
      use_operand_p arg_p;
      ssa_op_iter i;
      GIMPLE_type phi = gsi_stmt (si);
      tree res = gimple_phi_result (phi);

      if (virtual_operand_p (res))
	continue;

      FOR_EACH_PHI_ARG (arg_p, phi, i, SSA_OP_USE)
	{
	  tree arg = USE_FROM_PTR (arg_p);
	  if (TREE_CODE (arg) == SSA_NAME)
	    bitmap_set_bit (live, SSA_NAME_VERSION (arg));
	}

      bitmap_clear_bit (live, SSA_NAME_VERSION (res));
    }

  return need_assert;
}

/* Do an RPO walk over the function computing SSA name liveness
   on-the-fly and deciding on assert expressions to insert.
   Returns true if there are assert expressions to be inserted.  */

static bool
find_assert_locations (void)
{
  int *rpo = XNEWVEC (int, last_basic_block);
  int *bb_rpo = XNEWVEC (int, last_basic_block);
  int *last_rpo = XCNEWVEC (int, last_basic_block);
  int rpo_cnt, i;
  bool need_asserts;

  live = XCNEWVEC (sbitmap, last_basic_block);
  rpo_cnt = pre_and_rev_post_order_compute (NULL, rpo, false);
  for (i = 0; i < rpo_cnt; ++i)
    bb_rpo[rpo[i]] = i;

  need_asserts = false;
  for (i = rpo_cnt - 1; i >= 0; --i)
    {
      basic_block bb = BASIC_BLOCK (rpo[i]);
      edge e;
      edge_iterator ei;

      if (!live[rpo[i]])
	{
	  live[rpo[i]] = sbitmap_alloc (num_ssa_names);
	  bitmap_clear (live[rpo[i]]);
	}

      /* Process BB and update the live information with uses in
         this block.  */
      need_asserts |= find_assert_locations_1 (bb, live[rpo[i]]);

      /* Merge liveness into the predecessor blocks and free it.  */
      if (!bitmap_empty_p (live[rpo[i]]))
	{
	  int pred_rpo = i;
	  FOR_EACH_EDGE (e, ei, bb->preds)
	    {
	      int pred = e->src->index;
	      if ((e->flags & EDGE_DFS_BACK) || pred == ENTRY_BLOCK)
		continue;

	      if (!live[pred])
		{
		  live[pred] = sbitmap_alloc (num_ssa_names);
		  bitmap_clear (live[pred]);
		}
	      bitmap_ior (live[pred], live[pred], live[rpo[i]]);

	      if (bb_rpo[pred] < pred_rpo)
		pred_rpo = bb_rpo[pred];
	    }

	  /* Record the RPO number of the last visited block that needs
	     live information from this block.  */
	  last_rpo[rpo[i]] = pred_rpo;
	}
      else
	{
	  sbitmap_free (live[rpo[i]]);
	  live[rpo[i]] = NULL;
	}

      /* We can free all successors live bitmaps if all their
         predecessors have been visited already.  */
      FOR_EACH_EDGE (e, ei, bb->succs)
	if (last_rpo[e->dest->index] == i
	    && live[e->dest->index])
	  {
	    sbitmap_free (live[e->dest->index]);
	    live[e->dest->index] = NULL;
	  }
    }

  XDELETEVEC (rpo);
  XDELETEVEC (bb_rpo);
  XDELETEVEC (last_rpo);
  for (i = 0; i < last_basic_block; ++i)
    if (live[i])
      sbitmap_free (live[i]);
  XDELETEVEC (live);

  return need_asserts;
}

/* Create an ASSERT_EXPR for NAME and insert it in the location
   indicated by LOC.  Return true if we made any edge insertions.  */

static bool
process_assert_insertions_for (tree name, assert_locus_t loc)
{
  /* Build the comparison expression NAME_i COMP_CODE VAL.  */
  GIMPLE_type stmt;
  tree cond;
  GIMPLE_type assert_stmt;
  edge_iterator ei;
  edge e;

  /* If we have X <=> X do not insert an assert expr for that.  */
  if (loc->expr == loc->val)
    return false;

  cond = build2 (loc->comp_code, boolean_type_node, loc->expr, loc->val);
  assert_stmt = build_assert_expr_for (cond, name);
  if (loc->e)
    {
      /* We have been asked to insert the assertion on an edge.  This
	 is used only by COND_EXPR and SWITCH_EXPR assertions.  */
      gcc_checking_assert (gimple_code (gsi_stmt (loc->si)) == GIMPLE_COND
			   || (gimple_code (gsi_stmt (loc->si))
			       == GIMPLE_SWITCH));

      gsi_insert_on_edge (loc->e, assert_stmt);
      return true;
    }

  /* Otherwise, we can insert right after LOC->SI iff the
     statement must not be the last statement in the block.  */
  stmt = gsi_stmt (loc->si);
  if (!stmt_ends_bb_p (stmt))
    {
      gsi_insert_after (&loc->si, assert_stmt, GSI_SAME_STMT);
      return false;
    }

  /* If STMT must be the last statement in BB, we can only insert new
     assertions on the non-abnormal edge out of BB.  Note that since
     STMT is not control flow, there may only be one non-abnormal edge
     out of BB.  */
  FOR_EACH_EDGE (e, ei, loc->bb->succs)
    if (!(e->flags & EDGE_ABNORMAL))
      {
	gsi_insert_on_edge (e, assert_stmt);
	return true;
      }

  gcc_unreachable ();
}


/* Process all the insertions registered for every name N_i registered
   in NEED_ASSERT_FOR.  The list of assertions to be inserted are
   found in ASSERTS_FOR[i].  */

static void
process_assert_insertions (void)
{
  unsigned i;
  bitmap_iterator bi;
  bool update_edges_p = false;
  int num_asserts = 0;

  if (dump_file && (dump_flags & TDF_DETAILS))
    dump_all_asserts (dump_file);

  EXECUTE_IF_SET_IN_BITMAP (need_assert_for, 0, i, bi)
    {
      assert_locus_t loc = asserts_for[i];
      gcc_assert (loc);

      while (loc)
	{
	  assert_locus_t next = loc->next;
	  update_edges_p |= process_assert_insertions_for (ssa_name (i), loc);
	  free (loc);
	  loc = next;
	  num_asserts++;
	}
    }

  if (update_edges_p)
    gsi_commit_edge_inserts ();

  statistics_counter_event (cfun, "Number of ASSERT_EXPR expressions inserted",
			    num_asserts);
}


/* Traverse the flowgraph looking for conditional jumps to insert range
   expressions.  These range expressions are meant to provide information
   to optimizations that need to reason in terms of value ranges.  They
   will not be expanded into RTL.  For instance, given:

   x = ...
   y = ...
   if (x < y)
     y = x - 2;
   else
     x = y + 3;

   this pass will transform the code into:

   x = ...
   y = ...
   if (x < y)
    {
      x = ASSERT_EXPR <x, x < y>
      y = x - 2
    }
   else
    {
      y = ASSERT_EXPR <y, x <= y>
      x = y + 3
    }

   The idea is that once copy and constant propagation have run, other
   optimizations will be able to determine what ranges of values can 'x'
   take in different paths of the code, simply by checking the reaching
   definition of 'x'.  */

static void
insert_range_assertions (void)
{
  need_assert_for = BITMAP_ALLOC (NULL);
  asserts_for = XCNEWVEC (assert_locus_t, num_ssa_names);

  calculate_dominance_info (CDI_DOMINATORS);

  if (find_assert_locations ())
    {
      process_assert_insertions ();
      update_ssa (TODO_update_ssa_no_phi);
    }

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "\nSSA form after inserting ASSERT_EXPRs\n");
      dump_function_to_file (current_function_decl, dump_file, dump_flags);
    }

  free (asserts_for);
  BITMAP_FREE (need_assert_for);
}

/* Checks one ARRAY_REF in REF, located at LOCUS. Ignores flexible arrays
   and "struct" hacks. If VRP can determine that the
   array subscript is a constant, check if it is outside valid
   range. If the array subscript is a RANGE, warn if it is
   non-overlapping with valid range.
   IGNORE_OFF_BY_ONE is true if the ARRAY_REF is inside a ADDR_EXPR.  */

static void
check_array_ref (location_t location, tree ref, bool ignore_off_by_one)
{
  value_range_t* vr = NULL;
  tree low_sub, up_sub;
  tree low_bound, up_bound, up_bound_p1;
  tree base;

  if (TREE_NO_WARNING (ref))
    return;

  low_sub = up_sub = TREE_OPERAND (ref, 1);
  up_bound = array_ref_up_bound (ref);

  /* Can not check flexible arrays.  */
  if (!up_bound
      || TREE_CODE (up_bound) != INTEGER_CST)
    return;

  /* Accesses to trailing arrays via pointers may access storage
     beyond the types array bounds.  */
  base = get_base_address (ref);
  if (base && TREE_CODE (base) == MEM_REF)
    {
      tree cref, next = NULL_TREE;

      if (TREE_CODE (TREE_OPERAND (ref, 0)) != COMPONENT_REF)
	return;

      cref = TREE_OPERAND (ref, 0);
      if (TREE_CODE (TREE_TYPE (TREE_OPERAND (cref, 0))) == RECORD_TYPE)
	for (next = DECL_CHAIN (TREE_OPERAND (cref, 1));
	     next && TREE_CODE (next) != FIELD_DECL;
	     next = DECL_CHAIN (next))
	  ;

      /* If this is the last field in a struct type or a field in a
	 union type do not warn.  */
      if (!next)
	return;
    }

  low_bound = array_ref_low_bound (ref);
  up_bound_p1 = int_const_binop (PLUS_EXPR, up_bound, integer_one_node);

  if (TREE_CODE (low_sub) == SSA_NAME)
    {
      vr = get_value_range (low_sub);
      if (vr->type == VR_RANGE || vr->type == VR_ANTI_RANGE)
        {
          low_sub = vr->type == VR_RANGE ? vr->max : vr->min;
          up_sub = vr->type == VR_RANGE ? vr->min : vr->max;
        }
    }

  if (vr && vr->type == VR_ANTI_RANGE)
    {
      if (TREE_CODE (up_sub) == INTEGER_CST
          && tree_int_cst_lt (up_bound, up_sub)
          && TREE_CODE (low_sub) == INTEGER_CST
          && tree_int_cst_lt (low_sub, low_bound))
        {
          warning_at (location, OPT_Warray_bounds,
		      "array subscript is outside array bounds");
          TREE_NO_WARNING (ref) = 1;
        }
    }
  else if (TREE_CODE (up_sub) == INTEGER_CST
	   && (ignore_off_by_one
	       ? (tree_int_cst_lt (up_bound, up_sub)
		  && !tree_int_cst_equal (up_bound_p1, up_sub))
	       : (tree_int_cst_lt (up_bound, up_sub)
		  || tree_int_cst_equal (up_bound_p1, up_sub))))
    {
      warning_at (location, OPT_Warray_bounds,
		  "array subscript is above array bounds");
      TREE_NO_WARNING (ref) = 1;
    }
  else if (TREE_CODE (low_sub) == INTEGER_CST
           && tree_int_cst_lt (low_sub, low_bound))
    {
      warning_at (location, OPT_Warray_bounds,
		  "array subscript is below array bounds");
      TREE_NO_WARNING (ref) = 1;
    }
}

/* Searches if the expr T, located at LOCATION computes
   address of an ARRAY_REF, and call check_array_ref on it.  */

static void
search_for_addr_array (tree t, location_t location)
{
  while (TREE_CODE (t) == SSA_NAME)
    {
      GIMPLE_type g = SSA_NAME_DEF_STMT (t);

      if (gimple_code (g) != GIMPLE_ASSIGN)
	return;

      if (get_gimple_rhs_class (gimple_assign_rhs_code (g))
	  != GIMPLE_SINGLE_RHS)
	return;

      t = gimple_assign_rhs1 (g);
    }


  /* We are only interested in addresses of ARRAY_REF's.  */
  if (TREE_CODE (t) != ADDR_EXPR)
    return;

  /* Check each ARRAY_REFs in the reference chain. */
  do
    {
      if (TREE_CODE (t) == ARRAY_REF)
	check_array_ref (location, t, true /*ignore_off_by_one*/);

      t = TREE_OPERAND (t, 0);
    }
  while (handled_component_p (t));

  if (TREE_CODE (t) == MEM_REF
      && TREE_CODE (TREE_OPERAND (t, 0)) == ADDR_EXPR
      && !TREE_NO_WARNING (t))
    {
      tree tem = TREE_OPERAND (TREE_OPERAND (t, 0), 0);
      tree low_bound, up_bound, el_sz;
      double_int idx;
      if (TREE_CODE (TREE_TYPE (tem)) != ARRAY_TYPE
	  || TREE_CODE (TREE_TYPE (TREE_TYPE (tem))) == ARRAY_TYPE
	  || !TYPE_DOMAIN (TREE_TYPE (tem)))
	return;

      low_bound = TYPE_MIN_VALUE (TYPE_DOMAIN (TREE_TYPE (tem)));
      up_bound = TYPE_MAX_VALUE (TYPE_DOMAIN (TREE_TYPE (tem)));
      el_sz = TYPE_SIZE_UNIT (TREE_TYPE (TREE_TYPE (tem)));
      if (!low_bound
	  || TREE_CODE (low_bound) != INTEGER_CST
	  || !up_bound
	  || TREE_CODE (up_bound) != INTEGER_CST
	  || !el_sz
	  || TREE_CODE (el_sz) != INTEGER_CST)
	return;

      idx = mem_ref_offset (t);
      idx = idx.sdiv (tree_to_double_int (el_sz), TRUNC_DIV_EXPR);
      if (idx.slt (double_int_zero))
	{
	  warning_at (location, OPT_Warray_bounds,
		      "array subscript is below array bounds");
	  TREE_NO_WARNING (t) = 1;
	}
      else if (idx.sgt (tree_to_double_int (up_bound)
			- tree_to_double_int (low_bound)
			+ double_int_one))
	{
	  warning_at (location, OPT_Warray_bounds,
		      "array subscript is above array bounds");
	  TREE_NO_WARNING (t) = 1;
	}
    }
}

/* walk_tree() callback that checks if *TP is
   an ARRAY_REF inside an ADDR_EXPR (in which an array
   subscript one outside the valid range is allowed). Call
   check_array_ref for each ARRAY_REF found. The location is
   passed in DATA.  */

static tree
check_array_bounds (tree *tp, int *walk_subtree, void *data)
{
  tree t = *tp;
  struct walk_stmt_info *wi = (struct walk_stmt_info *) data;
  location_t location;

  if (EXPR_HAS_LOCATION (t))
    location = EXPR_LOCATION (t);
  else
    {
      location_t *locp = (location_t *) wi->info;
      location = *locp;
    }

  *walk_subtree = TRUE;

  if (TREE_CODE (t) == ARRAY_REF)
    check_array_ref (location, t, false /*ignore_off_by_one*/);

  if (TREE_CODE (t) == MEM_REF
      || (TREE_CODE (t) == RETURN_EXPR && TREE_OPERAND (t, 0)))
    search_for_addr_array (TREE_OPERAND (t, 0), location);

  if (TREE_CODE (t) == ADDR_EXPR)
    *walk_subtree = FALSE;

  return NULL_TREE;
}

/* Walk over all statements of all reachable BBs and call check_array_bounds
   on them.  */

static void
check_all_array_refs (void)
{
  basic_block bb;
  gimple_stmt_iterator si;

  FOR_EACH_BB (bb)
    {
      edge_iterator ei;
      edge e;
      bool executable = false;

      /* Skip blocks that were found to be unreachable.  */
      FOR_EACH_EDGE (e, ei, bb->preds)
	executable |= !!(e->flags & EDGE_EXECUTABLE);
      if (!executable)
	continue;

      for (si = gsi_start_bb (bb); !gsi_end_p (si); gsi_next (&si))
	{
     GIMPLE_type stmt = gsi_stmt (si);
	  struct walk_stmt_info wi;
	  if (!gimple_has_location (stmt))
	    continue;

	  if (is_gimple_call (stmt))
	    {
	      size_t i;
	      size_t n = gimple_call_num_args (stmt);
	      for (i = 0; i < n; i++)
		{
		  tree arg = gimple_call_arg (stmt, i);
		  search_for_addr_array (arg, gimple_location (stmt));
		}
	    }
	  else
	    {
	      memset (&wi, 0, sizeof (wi));
	      wi.info = CONST_CAST (void *, (const void *)
				    gimple_location_ptr (stmt));

	      walk_gimple_op (gsi_stmt (si),
			      check_array_bounds,
			      &wi);
	    }
	}
    }
}


/* Convert range assertion expressions into the implied copies and
   copy propagate away the copies.  Doing the trivial copy propagation
   here avoids the need to run the full copy propagation pass after
   VRP.

   FIXME, this will eventually lead to copy propagation removing the
   names that had useful range information attached to them.  For
   instance, if we had the assertion N_i = ASSERT_EXPR <N_j, N_j > 3>,
   then N_i will have the range [3, +INF].

   However, by converting the assertion into the implied copy
   operation N_i = N_j, we will then copy-propagate N_j into the uses
   of N_i and lose the range information.  We may want to hold on to
   ASSERT_EXPRs a little while longer as the ranges could be used in
   things like jump threading.

   The problem with keeping ASSERT_EXPRs around is that passes after
   VRP need to handle them appropriately.

   Another approach would be to make the range information a first
   class property of the SSA_NAME so that it can be queried from
   any pass.  This is made somewhat more complex by the need for
   multiple ranges to be associated with one SSA_NAME.  */

static void
remove_range_assertions (void)
{
  basic_block bb;
  gimple_stmt_iterator si;

  /* Note that the BSI iterator bump happens at the bottom of the
     loop and no bump is necessary if we're removing the statement
     referenced by the current BSI.  */
  FOR_EACH_BB (bb)
    for (si = gsi_start_bb (bb); !gsi_end_p (si);)
      {
   GIMPLE_type stmt = gsi_stmt (si);
   GIMPLE_type use_stmt;

	if (is_gimple_assign (stmt)
	    && gimple_assign_rhs_code (stmt) == ASSERT_EXPR)
	  {
	    tree rhs = gimple_assign_rhs1 (stmt);
	    tree var;
	    tree cond = fold (ASSERT_EXPR_COND (rhs));
	    use_operand_p use_p;
	    imm_use_iterator iter;

	    gcc_assert (cond != boolean_false_node);

	    /* Propagate the RHS into every use of the LHS.  */
	    var = ASSERT_EXPR_VAR (rhs);
	    FOR_EACH_IMM_USE_STMT (use_stmt, iter,
				   gimple_assign_lhs (stmt))
	      FOR_EACH_IMM_USE_ON_STMT (use_p, iter)
		{
		  SET_USE (use_p, var);
		  gcc_assert (TREE_CODE (var) == SSA_NAME);
		}

	    /* And finally, remove the copy, it is not needed.  */
	    gsi_remove (&si, true);
	    release_defs (stmt);
	  }
	else
	  gsi_next (&si);
      }
}


/* Return true if STMT is interesting for VRP.  */

static bool
stmt_interesting_for_vrp (GIMPLE_type stmt)
{
  if (gimple_code (stmt) == GIMPLE_PHI)
    {
      tree res = gimple_phi_result (stmt);
      return (!virtual_operand_p (res)
	      && (INTEGRAL_TYPE_P (TREE_TYPE (res))
		  || POINTER_TYPE_P (TREE_TYPE (res))));
    }
  else if (is_gimple_assign (stmt) || is_gimple_call (stmt))
    {
      tree lhs = gimple_get_lhs (stmt);

      /* In general, assignments with virtual operands are not useful
	 for deriving ranges, with the obvious exception of calls to
	 builtin functions.  */
      if (lhs && TREE_CODE (lhs) == SSA_NAME
	  && (INTEGRAL_TYPE_P (TREE_TYPE (lhs))
	      || POINTER_TYPE_P (TREE_TYPE (lhs)))
	  && ((is_gimple_call (stmt)
	       && gimple_call_fndecl (stmt) != NULL_TREE
	       && DECL_BUILT_IN (gimple_call_fndecl (stmt)))
	      || !gimple_vuse (stmt)))
	return true;
    }
  else if (gimple_code (stmt) == GIMPLE_COND
	   || gimple_code (stmt) == GIMPLE_SWITCH)
    return true;

  return false;
}


/* Initialize local data structures for VRP.  */

static void
vrp_initialize (void)
{
  basic_block bb;

  values_propagated = false;
  num_vr_values = num_ssa_names;
  vr_value = XCNEWVEC (value_range_t *, num_vr_values);
  vr_phi_edge_counts = XCNEWVEC (int, num_ssa_names);

  FOR_EACH_BB (bb)
    {
      gimple_stmt_iterator si;

      for (si = gsi_start_phis (bb); !gsi_end_p (si); gsi_next (&si))
	{
     GIMPLE_type phi = gsi_stmt (si);
	  if (!stmt_interesting_for_vrp (phi))
	    {
	      tree lhs = PHI_RESULT (phi);
	      set_value_range_to_varying (get_value_range (lhs));
	      prop_set_simulate_again (phi, false);
	    }
	  else
	    prop_set_simulate_again (phi, true);
	}

      for (si = gsi_start_bb (bb); !gsi_end_p (si); gsi_next (&si))
        {
     GIMPLE_type stmt = gsi_stmt (si);

 	  /* If the statement is a control insn, then we do not
 	     want to avoid simulating the statement once.  Failure
 	     to do so means that those edges will never get added.  */
	  if (stmt_ends_bb_p (stmt))
	    prop_set_simulate_again (stmt, true);
	  else if (!stmt_interesting_for_vrp (stmt))
	    {
	      ssa_op_iter i;
	      tree def;
	      FOR_EACH_SSA_TREE_OPERAND (def, stmt, i, SSA_OP_DEF)
		set_value_range_to_varying (get_value_range (def));
	      prop_set_simulate_again (stmt, false);
	    }
	  else
	    prop_set_simulate_again (stmt, true);
	}
    }
}

/* Return the singleton value-range for NAME or NAME.  */

static inline tree
vrp_valueize (tree name)
{
  if (TREE_CODE (name) == SSA_NAME)
    {
      value_range_t *vr = get_value_range (name);
      if (vr->type == VR_RANGE
	  && (vr->min == vr->max
	      || operand_equal_p (vr->min, vr->max, 0)))
	return vr->min;
    }
  return name;
}

/* Visit assignment STMT.  If it produces an interesting range, record
   the SSA name in *OUTPUT_P.  */

static enum ssa_prop_result
vrp_visit_assignment_or_call (GIMPLE_type stmt, tree *output_p)
{
  tree def, lhs;
  ssa_op_iter iter;
  enum gimple_code code = gimple_code (stmt);
  lhs = gimple_get_lhs (stmt);

  /* We only keep track of ranges in integral and pointer types.  */
  if (TREE_CODE (lhs) == SSA_NAME
      && ((INTEGRAL_TYPE_P (TREE_TYPE (lhs))
	   /* It is valid to have NULL MIN/MAX values on a type.  See
	      build_range_type.  */
	   && TYPE_MIN_VALUE (TREE_TYPE (lhs))
	   && TYPE_MAX_VALUE (TREE_TYPE (lhs)))
	  || POINTER_TYPE_P (TREE_TYPE (lhs))))
    {
      value_range_t new_vr = VR_INITIALIZER;

      /* Try folding the statement to a constant first.  */
      tree tem = gimple_fold_stmt_to_constant (stmt, vrp_valueize);
      if (tem && !is_overflow_infinity (tem))
	set_value_range (&new_vr, VR_RANGE, tem, tem, NULL);
      /* Then dispatch to value-range extracting functions.  */
      else if (code == GIMPLE_CALL)
	extract_range_basic (&new_vr, stmt);
      else
	extract_range_from_assignment (&new_vr, stmt);

      if (update_value_range (lhs, &new_vr))
	{
	  *output_p = lhs;

	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      fprintf (dump_file, "Found new range for ");
	      print_generic_expr (dump_file, lhs, 0);
	      fprintf (dump_file, ": ");
	      dump_value_range (dump_file, &new_vr);
	      fprintf (dump_file, "\n\n");
	    }

	  if (new_vr.type == VR_VARYING)
	    return SSA_PROP_VARYING;

	  return SSA_PROP_INTERESTING;
	}

      return SSA_PROP_NOT_INTERESTING;
    }

  /* Every other statement produces no useful ranges.  */
  FOR_EACH_SSA_TREE_OPERAND (def, stmt, iter, SSA_OP_DEF)
    set_value_range_to_varying (get_value_range (def));

  return SSA_PROP_VARYING;
}

/* Helper that gets the value range of the SSA_NAME with version I
   or a symbolic range containing the SSA_NAME only if the value range
   is varying or undefined.  */

static inline value_range_t
get_vr_for_comparison (int i)
{
  value_range_t vr = *get_value_range (ssa_name (i));

  /* If name N_i does not have a valid range, use N_i as its own
     range.  This allows us to compare against names that may
     have N_i in their ranges.  */
  if (vr.type == VR_VARYING || vr.type == VR_UNDEFINED)
    {
      vr.type = VR_RANGE;
      vr.min = ssa_name (i);
      vr.max = ssa_name (i);
    }

  return vr;
}

/* Compare all the value ranges for names equivalent to VAR with VAL
   using comparison code COMP.  Return the same value returned by
   compare_range_with_value, including the setting of
   *STRICT_OVERFLOW_P.  */

static tree
compare_name_with_value (enum tree_code comp, tree var, tree val,
			 bool *strict_overflow_p)
{
  bitmap_iterator bi;
  unsigned i;
  bitmap e;
  tree retval, t;
  int used_strict_overflow;
  bool sop;
  value_range_t equiv_vr;

  /* Get the set of equivalences for VAR.  */
  e = get_value_range (var)->equiv;

  /* Start at -1.  Set it to 0 if we do a comparison without relying
     on overflow, or 1 if all comparisons rely on overflow.  */
  used_strict_overflow = -1;

  /* Compare vars' value range with val.  */
  equiv_vr = get_vr_for_comparison (SSA_NAME_VERSION (var));
  sop = false;
  retval = compare_range_with_value (comp, &equiv_vr, val, &sop);
  if (retval)
    used_strict_overflow = sop ? 1 : 0;

  /* If the equiv set is empty we have done all work we need to do.  */
  if (e == NULL)
    {
      if (retval
	  && used_strict_overflow > 0)
	*strict_overflow_p = true;
      return retval;
    }

  EXECUTE_IF_SET_IN_BITMAP (e, 0, i, bi)
    {
      equiv_vr = get_vr_for_comparison (i);
      sop = false;
      t = compare_range_with_value (comp, &equiv_vr, val, &sop);
      if (t)
	{
	  /* If we get different answers from different members
	     of the equivalence set this check must be in a dead
	     code region.  Folding it to a trap representation
	     would be correct here.  For now just return don't-know.  */
	  if (retval != NULL
	      && t != retval)
	    {
	      retval = NULL_TREE;
	      break;
	    }
	  retval = t;

	  if (!sop)
	    used_strict_overflow = 0;
	  else if (used_strict_overflow < 0)
	    used_strict_overflow = 1;
	}
    }

  if (retval
      && used_strict_overflow > 0)
    *strict_overflow_p = true;

  return retval;
}


/* Given a comparison code COMP and names N1 and N2, compare all the
   ranges equivalent to N1 against all the ranges equivalent to N2
   to determine the value of N1 COMP N2.  Return the same value
   returned by compare_ranges.  Set *STRICT_OVERFLOW_P to indicate
   whether we relied on an overflow infinity in the comparison.  */


static tree
compare_names (enum tree_code comp, tree n1, tree n2,
	       bool *strict_overflow_p)
{
  tree t, retval;
  bitmap e1, e2;
  bitmap_iterator bi1, bi2;
  unsigned i1, i2;
  int used_strict_overflow;
  static bitmap_obstack *s_obstack = NULL;
  static bitmap s_e1 = NULL, s_e2 = NULL;

  /* Compare the ranges of every name equivalent to N1 against the
     ranges of every name equivalent to N2.  */
  e1 = get_value_range (n1)->equiv;
  e2 = get_value_range (n2)->equiv;

  /* Use the fake bitmaps if e1 or e2 are not available.  */
  if (s_obstack == NULL)
    {
      s_obstack = XNEW (bitmap_obstack);
      bitmap_obstack_initialize (s_obstack);
      s_e1 = BITMAP_ALLOC (s_obstack);
      s_e2 = BITMAP_ALLOC (s_obstack);
    }
  if (e1 == NULL)
    e1 = s_e1;
  if (e2 == NULL)
    e2 = s_e2;

  /* Add N1 and N2 to their own set of equivalences to avoid
     duplicating the body of the loop just to check N1 and N2
     ranges.  */
  bitmap_set_bit (e1, SSA_NAME_VERSION (n1));
  bitmap_set_bit (e2, SSA_NAME_VERSION (n2));

  /* If the equivalence sets have a common intersection, then the two
     names can be compared without checking their ranges.  */
  if (bitmap_intersect_p (e1, e2))
    {
      bitmap_clear_bit (e1, SSA_NAME_VERSION (n1));
      bitmap_clear_bit (e2, SSA_NAME_VERSION (n2));

      return (comp == EQ_EXPR || comp == GE_EXPR || comp == LE_EXPR)
	     ? boolean_true_node
	     : boolean_false_node;
    }

  /* Start at -1.  Set it to 0 if we do a comparison without relying
     on overflow, or 1 if all comparisons rely on overflow.  */
  used_strict_overflow = -1;

  /* Otherwise, compare all the equivalent ranges.  First, add N1 and
     N2 to their own set of equivalences to avoid duplicating the body
     of the loop just to check N1 and N2 ranges.  */
  EXECUTE_IF_SET_IN_BITMAP (e1, 0, i1, bi1)
    {
      value_range_t vr1 = get_vr_for_comparison (i1);

      t = retval = NULL_TREE;
      EXECUTE_IF_SET_IN_BITMAP (e2, 0, i2, bi2)
	{
	  bool sop = false;

	  value_range_t vr2 = get_vr_for_comparison (i2);

	  t = compare_ranges (comp, &vr1, &vr2, &sop);
	  if (t)
	    {
	      /* If we get different answers from different members
		 of the equivalence set this check must be in a dead
		 code region.  Folding it to a trap representation
		 would be correct here.  For now just return don't-know.  */
	      if (retval != NULL
		  && t != retval)
		{
		  bitmap_clear_bit (e1, SSA_NAME_VERSION (n1));
		  bitmap_clear_bit (e2, SSA_NAME_VERSION (n2));
		  return NULL_TREE;
		}
	      retval = t;

	      if (!sop)
		used_strict_overflow = 0;
	      else if (used_strict_overflow < 0)
		used_strict_overflow = 1;
	    }
	}

      if (retval)
	{
	  bitmap_clear_bit (e1, SSA_NAME_VERSION (n1));
	  bitmap_clear_bit (e2, SSA_NAME_VERSION (n2));
	  if (used_strict_overflow > 0)
	    *strict_overflow_p = true;
	  return retval;
	}
    }

  /* None of the equivalent ranges are useful in computing this
     comparison.  */
  bitmap_clear_bit (e1, SSA_NAME_VERSION (n1));
  bitmap_clear_bit (e2, SSA_NAME_VERSION (n2));
  return NULL_TREE;
}

/* Helper function for vrp_evaluate_conditional_warnv.  */

static tree
vrp_evaluate_conditional_warnv_with_ops_using_ranges (enum tree_code code,
						      tree op0, tree op1,
						      bool * strict_overflow_p)
{
  value_range_t *vr0, *vr1;

  vr0 = (TREE_CODE (op0) == SSA_NAME) ? get_value_range (op0) : NULL;
  vr1 = (TREE_CODE (op1) == SSA_NAME) ? get_value_range (op1) : NULL;

  if (vr0 && vr1)
    return compare_ranges (code, vr0, vr1, strict_overflow_p);
  else if (vr0 && vr1 == NULL)
    return compare_range_with_value (code, vr0, op1, strict_overflow_p);
  else if (vr0 == NULL && vr1)
    return (compare_range_with_value
	    (swap_tree_comparison (code), vr1, op0, strict_overflow_p));
  return NULL;
}

/* Helper function for vrp_evaluate_conditional_warnv. */

static tree
vrp_evaluate_conditional_warnv_with_ops (enum tree_code code, tree op0,
					 tree op1, bool use_equiv_p,
					 bool *strict_overflow_p, bool *only_ranges)
{
  tree ret;
  if (only_ranges)
    *only_ranges = true;

  /* We only deal with integral and pointer types.  */
  if (!INTEGRAL_TYPE_P (TREE_TYPE (op0))
      && !POINTER_TYPE_P (TREE_TYPE (op0)))
    return NULL_TREE;

  if (use_equiv_p)
    {
      if (only_ranges
          && (ret = vrp_evaluate_conditional_warnv_with_ops_using_ranges
	              (code, op0, op1, strict_overflow_p)))
	return ret;
      *only_ranges = false;
      if (TREE_CODE (op0) == SSA_NAME && TREE_CODE (op1) == SSA_NAME)
	return compare_names (code, op0, op1, strict_overflow_p);
      else if (TREE_CODE (op0) == SSA_NAME)
	return compare_name_with_value (code, op0, op1, strict_overflow_p);
      else if (TREE_CODE (op1) == SSA_NAME)
	return (compare_name_with_value
		(swap_tree_comparison (code), op1, op0, strict_overflow_p));
    }
  else
    return vrp_evaluate_conditional_warnv_with_ops_using_ranges (code, op0, op1,
								 strict_overflow_p);
  return NULL_TREE;
}

/* Given (CODE OP0 OP1) within STMT, try to simplify it based on value range
   information.  Return NULL if the conditional can not be evaluated.
   The ranges of all the names equivalent with the operands in COND
   will be used when trying to compute the value.  If the result is
   based on undefined signed overflow, issue a warning if
   appropriate.  */

static tree
vrp_evaluate_conditional (enum tree_code code, tree op0, tree op1, GIMPLE_type stmt)
{
  bool sop;
  tree ret;
  bool only_ranges;

  /* Some passes and foldings leak constants with overflow flag set
     into the IL.  Avoid doing wrong things with these and bail out.  */
  if ((TREE_CODE (op0) == INTEGER_CST
       && TREE_OVERFLOW (op0))
      || (TREE_CODE (op1) == INTEGER_CST
	  && TREE_OVERFLOW (op1)))
    return NULL_TREE;

  sop = false;
  ret = vrp_evaluate_conditional_warnv_with_ops (code, op0, op1, true, &sop,
  						 &only_ranges);

  if (ret && sop)
    {
      enum warn_strict_overflow_code wc;
      const char* warnmsg;

      if (is_gimple_min_invariant (ret))
	{
	  wc = WARN_STRICT_OVERFLOW_CONDITIONAL;
	  warnmsg = G_("assuming signed overflow does not occur when "
		       "simplifying conditional to constant");
	}
      else
	{
	  wc = WARN_STRICT_OVERFLOW_COMPARISON;
	  warnmsg = G_("assuming signed overflow does not occur when "
		       "simplifying conditional");
	}

      if (issue_strict_overflow_warning (wc))
	{
	  location_t location;

	  if (!gimple_has_location (stmt))
	    location = input_location;
	  else
	    location = gimple_location (stmt);
	  warning_at (location, OPT_Wstrict_overflow, "%s", warnmsg);
	}
    }

  if (warn_type_limits
      && ret && only_ranges
      && TREE_CODE_CLASS (code) == tcc_comparison
      && TREE_CODE (op0) == SSA_NAME)
    {
      /* If the comparison is being folded and the operand on the LHS
	 is being compared against a constant value that is outside of
	 the natural range of OP0's type, then the predicate will
	 always fold regardless of the value of OP0.  If -Wtype-limits
	 was specified, emit a warning.  */
      tree type = TREE_TYPE (op0);
      value_range_t *vr0 = get_value_range (op0);

      if (vr0->type != VR_VARYING
	  && INTEGRAL_TYPE_P (type)
	  && vrp_val_is_min (vr0->min)
	  && vrp_val_is_max (vr0->max)
	  && is_gimple_min_invariant (op1))
	{
	  location_t location;

	  if (!gimple_has_location (stmt))
	    location = input_location;
	  else
	    location = gimple_location (stmt);

	  warning_at (location, OPT_Wtype_limits,
		      integer_zerop (ret)
		      ? G_("comparison always false "
                           "due to limited range of data type")
		      : G_("comparison always true "
                           "due to limited range of data type"));
	}
    }

  return ret;
}


/* Visit conditional statement STMT.  If we can determine which edge
   will be taken out of STMT's basic block, record it in
   *TAKEN_EDGE_P and return SSA_PROP_INTERESTING.  Otherwise, return
   SSA_PROP_VARYING.  */

static enum ssa_prop_result
vrp_visit_cond_stmt (GIMPLE_type stmt, edge *taken_edge_p)
{
  tree val;
  bool sop;

  *taken_edge_p = NULL;

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      tree use;
      ssa_op_iter i;

      fprintf (dump_file, "\nVisiting conditional with predicate: ");
      print_gimple_stmt (dump_file, stmt, 0, 0);
      fprintf (dump_file, "\nWith known ranges\n");

      FOR_EACH_SSA_TREE_OPERAND (use, stmt, i, SSA_OP_USE)
	{
	  fprintf (dump_file, "\t");
	  print_generic_expr (dump_file, use, 0);
	  fprintf (dump_file, ": ");
	  dump_value_range (dump_file, vr_value[SSA_NAME_VERSION (use)]);
	}

      fprintf (dump_file, "\n");
    }

  /* Compute the value of the predicate COND by checking the known
     ranges of each of its operands.

     Note that we cannot evaluate all the equivalent ranges here
     because those ranges may not yet be final and with the current
     propagation strategy, we cannot determine when the value ranges
     of the names in the equivalence set have changed.

     For instance, given the following code fragment

        i_5 = PHI <8, i_13>
	...
     	i_14 = ASSERT_EXPR <i_5, i_5 != 0>
	if (i_14 == 1)
	  ...

     Assume that on the first visit to i_14, i_5 has the temporary
     range [8, 8] because the second argument to the PHI function is
     not yet executable.  We derive the range ~[0, 0] for i_14 and the
     equivalence set { i_5 }.  So, when we visit 'if (i_14 == 1)' for
     the first time, since i_14 is equivalent to the range [8, 8], we
     determine that the predicate is always false.

     On the next round of propagation, i_13 is determined to be
     VARYING, which causes i_5 to drop down to VARYING.  So, another
     visit to i_14 is scheduled.  In this second visit, we compute the
     exact same range and equivalence set for i_14, namely ~[0, 0] and
     { i_5 }.  But we did not have the previous range for i_5
     registered, so vrp_visit_assignment thinks that the range for
     i_14 has not changed.  Therefore, the predicate 'if (i_14 == 1)'
     is not visited again, which stops propagation from visiting
     statements in the THEN clause of that if().

     To properly fix this we would need to keep the previous range
     value for the names in the equivalence set.  This way we would've
     discovered that from one visit to the other i_5 changed from
     range [8, 8] to VR_VARYING.

     However, fixing this apparent limitation may not be worth the
     additional checking.  Testing on several code bases (GCC, DLV,
     MICO, TRAMP3D and SPEC2000) showed that doing this results in
     4 more predicates folded in SPEC.  */
  sop = false;

  val = vrp_evaluate_conditional_warnv_with_ops (gimple_cond_code (stmt),
						 gimple_cond_lhs (stmt),
						 gimple_cond_rhs (stmt),
						 false, &sop, NULL);
  if (val)
    {
      if (!sop)
	*taken_edge_p = find_taken_edge (gimple_bb (stmt), val);
      else
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file,
		     "\nIgnoring predicate evaluation because "
		     "it assumes that signed overflow is undefined");
	  val = NULL_TREE;
	}
    }

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "\nPredicate evaluates to: ");
      if (val == NULL_TREE)
	fprintf (dump_file, "DON'T KNOW\n");
      else
	print_generic_stmt (dump_file, val, 0);
    }

  return (*taken_edge_p) ? SSA_PROP_INTERESTING : SSA_PROP_VARYING;
}

/* Searches the case label vector VEC for the index *IDX of the CASE_LABEL
   that includes the value VAL.  The search is restricted to the range
   [START_IDX, n - 1] where n is the size of VEC.

   If there is a CASE_LABEL for VAL, its index is placed in IDX and true is
   returned.

   If there is no CASE_LABEL for VAL and there is one that is larger than VAL,
   it is placed in IDX and false is returned.

   If VAL is larger than any CASE_LABEL, n is placed on IDX and false is
   returned. */

static bool
find_case_label_index (GIMPLE_type stmt, size_t start_idx, tree val, size_t *idx)
{
  size_t n = gimple_switch_num_labels (stmt);
  size_t low, high;

  /* Find case label for minimum of the value range or the next one.
     At each iteration we are searching in [low, high - 1]. */

  for (low = start_idx, high = n; high != low; )
    {
      tree t;
      int cmp;
      /* Note that i != high, so we never ask for n. */
      size_t i = (high + low) / 2;
      t = gimple_switch_label (stmt, i);

      /* Cache the result of comparing CASE_LOW and val.  */
      cmp = tree_int_cst_compare (CASE_LOW (t), val);

      if (cmp == 0)
	{
	  /* Ranges cannot be empty. */
	  *idx = i;
	  return true;
	}
      else if (cmp > 0)
        high = i;
      else
	{
	  low = i + 1;
	  if (CASE_HIGH (t) != NULL
	      && tree_int_cst_compare (CASE_HIGH (t), val) >= 0)
	    {
	      *idx = i;
	      return true;
	    }
        }
    }

  *idx = high;
  return false;
}

/* Searches the case label vector VEC for the range of CASE_LABELs that is used
   for values between MIN and MAX. The first index is placed in MIN_IDX. The
   last index is placed in MAX_IDX. If the range of CASE_LABELs is empty
   then MAX_IDX < MIN_IDX.
   Returns true if the default label is not needed. */

static bool
find_case_label_range (GIMPLE_type stmt, tree min, tree max, size_t *min_idx,
		       size_t *max_idx)
{
  size_t i, j;
  bool min_take_default = !find_case_label_index (stmt, 1, min, &i);
  bool max_take_default = !find_case_label_index (stmt, i, max, &j);

  if (i == j
      && min_take_default
      && max_take_default)
    {
      /* Only the default case label reached.
         Return an empty range. */
      *min_idx = 1;
      *max_idx = 0;
      return false;
    }
  else
    {
      bool take_default = min_take_default || max_take_default;
      tree low, high;
      size_t k;

      if (max_take_default)
	j--;

      /* If the case label range is continuous, we do not need
	 the default case label.  Verify that.  */
      high = CASE_LOW (gimple_switch_label (stmt, i));
      if (CASE_HIGH (gimple_switch_label (stmt, i)))
	high = CASE_HIGH (gimple_switch_label (stmt, i));
      for (k = i + 1; k <= j; ++k)
	{
	  low = CASE_LOW (gimple_switch_label (stmt, k));
	  if (!integer_onep (int_const_binop (MINUS_EXPR, low, high)))
	    {
	      take_default = true;
	      break;
	    }
	  high = low;
	  if (CASE_HIGH (gimple_switch_label (stmt, k)))
	    high = CASE_HIGH (gimple_switch_label (stmt, k));
	}

      *min_idx = i;
      *max_idx = j;
      return !take_default;
    }
}

/* Searches the case label vector VEC for the ranges of CASE_LABELs that are
   used in range VR.  The indices are placed in MIN_IDX1, MAX_IDX, MIN_IDX2 and
   MAX_IDX2.  If the ranges of CASE_LABELs are empty then MAX_IDX1 < MIN_IDX1.
   Returns true if the default label is not needed.  */

static bool
find_case_label_ranges (GIMPLE_type stmt, value_range_t *vr, size_t *min_idx1,
			size_t *max_idx1, size_t *min_idx2,
			size_t *max_idx2)
{
  size_t i, j, k, l;
  unsigned int n = gimple_switch_num_labels (stmt);
  bool take_default;
  tree case_low, case_high;
  tree min = vr->min, max = vr->max;

  gcc_checking_assert (vr->type == VR_RANGE || vr->type == VR_ANTI_RANGE);

  take_default = !find_case_label_range (stmt, min, max, &i, &j);

  /* Set second range to emtpy.  */
  *min_idx2 = 1;
  *max_idx2 = 0;

  if (vr->type == VR_RANGE)
    {
      *min_idx1 = i;
      *max_idx1 = j;
      return !take_default;
    }

  /* Set first range to all case labels.  */
  *min_idx1 = 1;
  *max_idx1 = n - 1;

  if (i > j)
    return false;

  /* Make sure all the values of case labels [i , j] are contained in
     range [MIN, MAX].  */
  case_low = CASE_LOW (gimple_switch_label (stmt, i));
  case_high = CASE_HIGH (gimple_switch_label (stmt, j));
  if (tree_int_cst_compare (case_low, min) < 0)
    i += 1;
  if (case_high != NULL_TREE
      && tree_int_cst_compare (max, case_high) < 0)
    j -= 1;

  if (i > j)
    return false;

  /* If the range spans case labels [i, j], the corresponding anti-range spans
     the labels [1, i - 1] and [j + 1, n -  1].  */
  k = j + 1;
  l = n - 1;
  if (k > l)
    {
      k = 1;
      l = 0;
    }

  j = i - 1;
  i = 1;
  if (i > j)
    {
      i = k;
      j = l;
      k = 1;
      l = 0;
    }

  *min_idx1 = i;
  *max_idx1 = j;
  *min_idx2 = k;
  *max_idx2 = l;
  return false;
}

/* Visit switch statement STMT.  If we can determine which edge
   will be taken out of STMT's basic block, record it in
   *TAKEN_EDGE_P and return SSA_PROP_INTERESTING.  Otherwise, return
   SSA_PROP_VARYING.  */

static enum ssa_prop_result
vrp_visit_switch_stmt (GIMPLE_type stmt, edge *taken_edge_p)
{
  tree op, val;
  value_range_t *vr;
  size_t i = 0, j = 0, k, l;
  bool take_default;

  *taken_edge_p = NULL;
  op = gimple_switch_index (stmt);
  if (TREE_CODE (op) != SSA_NAME)
    return SSA_PROP_VARYING;

  vr = get_value_range (op);
  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "\nVisiting switch expression with operand ");
      print_generic_expr (dump_file, op, 0);
      fprintf (dump_file, " with known range ");
      dump_value_range (dump_file, vr);
      fprintf (dump_file, "\n");
    }

  if ((vr->type != VR_RANGE
       && vr->type != VR_ANTI_RANGE)
      || symbolic_range_p (vr))
    return SSA_PROP_VARYING;

  /* Find the single edge that is taken from the switch expression.  */
  take_default = !find_case_label_ranges (stmt, vr, &i, &j, &k, &l);

  /* Check if the range spans no CASE_LABEL. If so, we only reach the default
     label */
  if (j < i)
    {
      gcc_assert (take_default);
      val = gimple_switch_default_label (stmt);
    }
  else
    {
      /* Check if labels with index i to j and maybe the default label
	 are all reaching the same label.  */

      val = gimple_switch_label (stmt, i);
      if (take_default
	  && CASE_LABEL (gimple_switch_default_label (stmt))
	  != CASE_LABEL (val))
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file, "  not a single destination for this "
		     "range\n");
          return SSA_PROP_VARYING;
	}
      for (++i; i <= j; ++i)
        {
          if (CASE_LABEL (gimple_switch_label (stmt, i)) != CASE_LABEL (val))
	    {
	      if (dump_file && (dump_flags & TDF_DETAILS))
		fprintf (dump_file, "  not a single destination for this "
			 "range\n");
	      return SSA_PROP_VARYING;
	    }
        }
      for (; k <= l; ++k)
        {
          if (CASE_LABEL (gimple_switch_label (stmt, k)) != CASE_LABEL (val))
	    {
	      if (dump_file && (dump_flags & TDF_DETAILS))
		fprintf (dump_file, "  not a single destination for this "
			 "range\n");
	      return SSA_PROP_VARYING;
	    }
        }
    }

  *taken_edge_p = find_edge (gimple_bb (stmt),
			     label_to_block (CASE_LABEL (val)));

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "  will take edge to ");
      print_generic_stmt (dump_file, CASE_LABEL (val), 0);
    }

  return SSA_PROP_INTERESTING;
}


/* Evaluate statement STMT.  If the statement produces a useful range,
   return SSA_PROP_INTERESTING and record the SSA name with the
   interesting range into *OUTPUT_P.

   If STMT is a conditional branch and we can determine its truth
   value, the taken edge is recorded in *TAKEN_EDGE_P.

   If STMT produces a varying value, return SSA_PROP_VARYING.  */

static enum ssa_prop_result
vrp_visit_stmt (GIMPLE_type stmt, edge *taken_edge_p, tree *output_p)
{
  tree def;
  ssa_op_iter iter;

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "\nVisiting statement:\n");
      print_gimple_stmt (dump_file, stmt, 0, dump_flags);
      fprintf (dump_file, "\n");
    }

  if (!stmt_interesting_for_vrp (stmt))
    gcc_assert (stmt_ends_bb_p (stmt));
  else if (is_gimple_assign (stmt) || is_gimple_call (stmt))
    {
      /* In general, assignments with virtual operands are not useful
	 for deriving ranges, with the obvious exception of calls to
	 builtin functions.  */
      if ((is_gimple_call (stmt)
	   && gimple_call_fndecl (stmt) != NULL_TREE
	   && DECL_BUILT_IN (gimple_call_fndecl (stmt)))
	  || !gimple_vuse (stmt))
	return vrp_visit_assignment_or_call (stmt, output_p);
    }
  else if (gimple_code (stmt) == GIMPLE_COND)
    return vrp_visit_cond_stmt (stmt, taken_edge_p);
  else if (gimple_code (stmt) == GIMPLE_SWITCH)
    return vrp_visit_switch_stmt (stmt, taken_edge_p);

  /* All other statements produce nothing of interest for VRP, so mark
     their outputs varying and prevent further simulation.  */
  FOR_EACH_SSA_TREE_OPERAND (def, stmt, iter, SSA_OP_DEF)
    set_value_range_to_varying (get_value_range (def));

  return SSA_PROP_VARYING;
}

/* Union the two value-ranges { *VR0TYPE, *VR0MIN, *VR0MAX } and
   { VR1TYPE, VR0MIN, VR0MAX } and store the result
   in { *VR0TYPE, *VR0MIN, *VR0MAX }.  This may not be the smallest
   possible such range.  The resulting range is not canonicalized.  */

static void
union_ranges (enum value_range_type *vr0type,
	      tree *vr0min, tree *vr0max,
	      enum value_range_type vr1type,
	      tree vr1min, tree vr1max)
{
  bool mineq = operand_equal_p (*vr0min, vr1min, 0);
  bool maxeq = operand_equal_p (*vr0max, vr1max, 0);

  /* [] is vr0, () is vr1 in the following classification comments.  */
  if (mineq && maxeq)
    {
      /* [(  )] */
      if (*vr0type == vr1type)
	/* Nothing to do for equal ranges.  */
	;
      else if ((*vr0type == VR_RANGE
		&& vr1type == VR_ANTI_RANGE)
	       || (*vr0type == VR_ANTI_RANGE
		   && vr1type == VR_RANGE))
	{
	  /* For anti-range with range union the result is varying.  */
	  goto give_up;
	}
      else
	gcc_unreachable ();
    }
  else if (operand_less_p (*vr0max, vr1min) == 1
	   || operand_less_p (vr1max, *vr0min) == 1)
    {
      /* [ ] ( ) or ( ) [ ]
	 If the ranges have an empty intersection, result of the union
	 operation is the anti-range or if both are anti-ranges
	 it covers all.  */
      if (*vr0type == VR_ANTI_RANGE
	  && vr1type == VR_ANTI_RANGE)
	goto give_up;
      else if (*vr0type == VR_ANTI_RANGE
	       && vr1type == VR_RANGE)
	;
      else if (*vr0type == VR_RANGE
	       && vr1type == VR_ANTI_RANGE)
	{
	  *vr0type = vr1type;
	  *vr0min = vr1min;
	  *vr0max = vr1max;
	}
      else if (*vr0type == VR_RANGE
	       && vr1type == VR_RANGE)
	{
	  /* The result is the convex hull of both ranges.  */
	  if (operand_less_p (*vr0max, vr1min) == 1)
	    {
	      /* If the result can be an anti-range, create one.  */
	      if (TREE_CODE (*vr0max) == INTEGER_CST
		  && TREE_CODE (vr1min) == INTEGER_CST
		  && vrp_val_is_min (*vr0min)
		  && vrp_val_is_max (vr1max))
		{
		  tree min = int_const_binop (PLUS_EXPR,
					      *vr0max, integer_one_node);
		  tree max = int_const_binop (MINUS_EXPR,
					      vr1min, integer_one_node);
		  if (!operand_less_p (max, min))
		    {
		      *vr0type = VR_ANTI_RANGE;
		      *vr0min = min;
		      *vr0max = max;
		    }
		  else
		    *vr0max = vr1max;
		}
	      else
		*vr0max = vr1max;
	    }
	  else
	    {
	      /* If the result can be an anti-range, create one.  */
	      if (TREE_CODE (vr1max) == INTEGER_CST
		  && TREE_CODE (*vr0min) == INTEGER_CST
		  && vrp_val_is_min (vr1min)
		  && vrp_val_is_max (*vr0max))
		{
		  tree min = int_const_binop (PLUS_EXPR,
					      vr1max, integer_one_node);
		  tree max = int_const_binop (MINUS_EXPR,
					      *vr0min, integer_one_node);
		  if (!operand_less_p (max, min))
		    {
		      *vr0type = VR_ANTI_RANGE;
		      *vr0min = min;
		      *vr0max = max;
		    }
		  else
		    *vr0min = vr1min;
		}
	      else
		*vr0min = vr1min;
	    }
	}
      else
	gcc_unreachable ();
    }
  else if ((maxeq || operand_less_p (vr1max, *vr0max) == 1)
	   && (mineq || operand_less_p (*vr0min, vr1min) == 1))
    {
      /* [ (  ) ] or [(  ) ] or [ (  )] */
      if (*vr0type == VR_RANGE
	  && vr1type == VR_RANGE)
	;
      else if (*vr0type == VR_ANTI_RANGE
	       && vr1type == VR_ANTI_RANGE)
	{
	  *vr0type = vr1type;
	  *vr0min = vr1min;
	  *vr0max = vr1max;
	}
      else if (*vr0type == VR_ANTI_RANGE
	       && vr1type == VR_RANGE)
	{
	  /* Arbitrarily choose the right or left gap.  */
	  if (!mineq && TREE_CODE (vr1min) == INTEGER_CST)
	    *vr0max = int_const_binop (MINUS_EXPR, vr1min, integer_one_node);
	  else if (!maxeq && TREE_CODE (vr1max) == INTEGER_CST)
	    *vr0min = int_const_binop (PLUS_EXPR, vr1max, integer_one_node);
	  else
	    goto give_up;
	}
      else if (*vr0type == VR_RANGE
	       && vr1type == VR_ANTI_RANGE)
	/* The result covers everything.  */
	goto give_up;
      else
	gcc_unreachable ();
    }
  else if ((maxeq || operand_less_p (*vr0max, vr1max) == 1)
	   && (mineq || operand_less_p (vr1min, *vr0min) == 1))
    {
      /* ( [  ] ) or ([  ] ) or ( [  ]) */
      if (*vr0type == VR_RANGE
	  && vr1type == VR_RANGE)
	{
	  *vr0type = vr1type;
	  *vr0min = vr1min;
	  *vr0max = vr1max;
	}
      else if (*vr0type == VR_ANTI_RANGE
	       && vr1type == VR_ANTI_RANGE)
	;
      else if (*vr0type == VR_RANGE
	       && vr1type == VR_ANTI_RANGE)
	{
	  *vr0type = VR_ANTI_RANGE;
	  if (!mineq && TREE_CODE (*vr0min) == INTEGER_CST)
	    {
	      *vr0max = int_const_binop (MINUS_EXPR, *vr0min, integer_one_node);
	      *vr0min = vr1min;
	    }
	  else if (!maxeq && TREE_CODE (*vr0max) == INTEGER_CST)
	    {
	      *vr0min = int_const_binop (PLUS_EXPR, *vr0max, integer_one_node);
	      *vr0max = vr1max;
	    }
	  else
	    goto give_up;
	}
      else if (*vr0type == VR_ANTI_RANGE
	       && vr1type == VR_RANGE)
	/* The result covers everything.  */
	goto give_up;
      else
	gcc_unreachable ();
    }
  else if ((operand_less_p (vr1min, *vr0max) == 1
	    || operand_equal_p (vr1min, *vr0max, 0))
	   && operand_less_p (*vr0min, vr1min) == 1)
    {
      /* [  (  ]  ) or [   ](   ) */
      if (*vr0type == VR_RANGE
	  && vr1type == VR_RANGE)
	*vr0max = vr1max;
      else if (*vr0type == VR_ANTI_RANGE
	       && vr1type == VR_ANTI_RANGE)
	*vr0min = vr1min;
      else if (*vr0type == VR_ANTI_RANGE
	       && vr1type == VR_RANGE)
	{
	  if (TREE_CODE (vr1min) == INTEGER_CST)
	    *vr0max = int_const_binop (MINUS_EXPR, vr1min, integer_one_node);
	  else
	    goto give_up;
	}
      else if (*vr0type == VR_RANGE
	       && vr1type == VR_ANTI_RANGE)
	{
	  if (TREE_CODE (*vr0max) == INTEGER_CST)
	    {
	      *vr0type = vr1type;
	      *vr0min = int_const_binop (PLUS_EXPR, *vr0max, integer_one_node);
	      *vr0max = vr1max;
	    }
	  else
	    goto give_up;
	}
      else
	gcc_unreachable ();
    }
  else if ((operand_less_p (*vr0min, vr1max) == 1
	    || operand_equal_p (*vr0min, vr1max, 0))
	   && operand_less_p (vr1min, *vr0min) == 1)
    {
      /* (  [  )  ] or (   )[   ] */
      if (*vr0type == VR_RANGE
	  && vr1type == VR_RANGE)
	*vr0min = vr1min;
      else if (*vr0type == VR_ANTI_RANGE
	       && vr1type == VR_ANTI_RANGE)
	*vr0max = vr1max;
      else if (*vr0type == VR_ANTI_RANGE
	       && vr1type == VR_RANGE)
	{
	  if (TREE_CODE (vr1max) == INTEGER_CST)
	    *vr0min = int_const_binop (PLUS_EXPR, vr1max, integer_one_node);
	  else
	    goto give_up;
	}
      else if (*vr0type == VR_RANGE
	       && vr1type == VR_ANTI_RANGE)
	{
	  if (TREE_CODE (*vr0min) == INTEGER_CST)
	    {
	      *vr0type = vr1type;
	      *vr0min = vr1min;
	      *vr0max = int_const_binop (MINUS_EXPR, *vr0min, integer_one_node);
	    }
	  else
	    goto give_up;
	}
      else
	gcc_unreachable ();
    }
  else
    goto give_up;

  return;

give_up:
  *vr0type = VR_VARYING;
  *vr0min = NULL_TREE;
  *vr0max = NULL_TREE;
}

/* Intersect the two value-ranges { *VR0TYPE, *VR0MIN, *VR0MAX } and
   { VR1TYPE, VR0MIN, VR0MAX } and store the result
   in { *VR0TYPE, *VR0MIN, *VR0MAX }.  This may not be the smallest
   possible such range.  The resulting range is not canonicalized.  */

static void
intersect_ranges (enum value_range_type *vr0type,
		  tree *vr0min, tree *vr0max,
		  enum value_range_type vr1type,
		  tree vr1min, tree vr1max)
{
  bool mineq = operand_equal_p (*vr0min, vr1min, 0);
  bool maxeq = operand_equal_p (*vr0max, vr1max, 0);

  /* [] is vr0, () is vr1 in the following classification comments.  */
  if (mineq && maxeq)
    {
      /* [(  )] */
      if (*vr0type == vr1type)
	/* Nothing to do for equal ranges.  */
	;
      else if ((*vr0type == VR_RANGE
		&& vr1type == VR_ANTI_RANGE)
	       || (*vr0type == VR_ANTI_RANGE
		   && vr1type == VR_RANGE))
	{
	  /* For anti-range with range intersection the result is empty.  */
	  *vr0type = VR_UNDEFINED;
	  *vr0min = NULL_TREE;
	  *vr0max = NULL_TREE;
	}
      else
	gcc_unreachable ();
    }
  else if (operand_less_p (*vr0max, vr1min) == 1
	   || operand_less_p (vr1max, *vr0min) == 1)
    {
      /* [ ] ( ) or ( ) [ ]
	 If the ranges have an empty intersection, the result of the
	 intersect operation is the range for intersecting an
	 anti-range with a range or empty when intersecting two ranges.  */
      if (*vr0type == VR_RANGE
	  && vr1type == VR_ANTI_RANGE)
	;
      else if (*vr0type == VR_ANTI_RANGE
	       && vr1type == VR_RANGE)
	{
	  *vr0type = vr1type;
	  *vr0min = vr1min;
	  *vr0max = vr1max;
	}
      else if (*vr0type == VR_RANGE
	       && vr1type == VR_RANGE)
	{
	  *vr0type = VR_UNDEFINED;
	  *vr0min = NULL_TREE;
	  *vr0max = NULL_TREE;
	}
      else if (*vr0type == VR_ANTI_RANGE
	       && vr1type == VR_ANTI_RANGE)
	{
	  /* If the anti-ranges are adjacent to each other merge them.  */
	  if (TREE_CODE (*vr0max) == INTEGER_CST
	      && TREE_CODE (vr1min) == INTEGER_CST
	      && operand_less_p (*vr0max, vr1min) == 1
	      && integer_onep (int_const_binop (MINUS_EXPR,
						vr1min, *vr0max)))
	    *vr0max = vr1max;
	  else if (TREE_CODE (vr1max) == INTEGER_CST
		   && TREE_CODE (*vr0min) == INTEGER_CST
		   && operand_less_p (vr1max, *vr0min) == 1
		   && integer_onep (int_const_binop (MINUS_EXPR,
						     *vr0min, vr1max)))
	    *vr0min = vr1min;
	  /* Else arbitrarily take VR0.  */
	}
    }
  else if ((maxeq || operand_less_p (vr1max, *vr0max) == 1)
	   && (mineq || operand_less_p (*vr0min, vr1min) == 1))
    {
      /* [ (  ) ] or [(  ) ] or [ (  )] */
      if (*vr0type == VR_RANGE
	  && vr1type == VR_RANGE)
	{
	  /* If both are ranges the result is the inner one.  */
	  *vr0type = vr1type;
	  *vr0min = vr1min;
	  *vr0max = vr1max;
	}
      else if (*vr0type == VR_RANGE
	       && vr1type == VR_ANTI_RANGE)
	{
	  /* Choose the right gap if the left one is empty.  */
	  if (mineq)
	    {
	      if (TREE_CODE (vr1max) == INTEGER_CST)
		*vr0min = int_const_binop (PLUS_EXPR, vr1max, integer_one_node);
	      else
		*vr0min = vr1max;
	    }
	  /* Choose the left gap if the right one is empty.  */
	  else if (maxeq)
	    {
	      if (TREE_CODE (vr1min) == INTEGER_CST)
		*vr0max = int_const_binop (MINUS_EXPR, vr1min,
					   integer_one_node);
	      else
		*vr0max = vr1min;
	    }
	  /* Choose the anti-range if the range is effectively varying.  */
	  else if (vrp_val_is_min (*vr0min)
		   && vrp_val_is_max (*vr0max))
	    {
	      *vr0type = vr1type;
	      *vr0min = vr1min;
	      *vr0max = vr1max;
	    }
	  /* Else choose the range.  */
	}
      else if (*vr0type == VR_ANTI_RANGE
	       && vr1type == VR_ANTI_RANGE)
	/* If both are anti-ranges the result is the outer one.  */
	;
      else if (*vr0type == VR_ANTI_RANGE
	       && vr1type == VR_RANGE)
	{
	  /* The intersection is empty.  */
	  *vr0type = VR_UNDEFINED;
	  *vr0min = NULL_TREE;
	  *vr0max = NULL_TREE;
	}
      else
	gcc_unreachable ();
    }
  else if ((maxeq || operand_less_p (*vr0max, vr1max) == 1)
	   && (mineq || operand_less_p (vr1min, *vr0min) == 1))
    {
      /* ( [  ] ) or ([  ] ) or ( [  ]) */
      if (*vr0type == VR_RANGE
	  && vr1type == VR_RANGE)
	/* Choose the inner range.  */
	;
      else if (*vr0type == VR_ANTI_RANGE
	       && vr1type == VR_RANGE)
	{
	  /* Choose the right gap if the left is empty.  */
	  if (mineq)
	    {
	      *vr0type = VR_RANGE;
	      if (TREE_CODE (*vr0max) == INTEGER_CST)
		*vr0min = int_const_binop (PLUS_EXPR, *vr0max,
					   integer_one_node);
	      else
		*vr0min = *vr0max;
	      *vr0max = vr1max;
	    }
	  /* Choose the left gap if the right is empty.  */
	  else if (maxeq)
	    {
	      *vr0type = VR_RANGE;
	      if (TREE_CODE (*vr0min) == INTEGER_CST)
		*vr0max = int_const_binop (MINUS_EXPR, *vr0min,
					   integer_one_node);
	      else
		*vr0max = *vr0min;
	      *vr0min = vr1min;
	    }
	  /* Choose the anti-range if the range is effectively varying.  */
	  else if (vrp_val_is_min (vr1min)
		   && vrp_val_is_max (vr1max))
	    ;
	  /* Else choose the range.  */
	  else
	    {
	      *vr0type = vr1type;
	      *vr0min = vr1min;
	      *vr0max = vr1max;
	    }
	}
      else if (*vr0type == VR_ANTI_RANGE
	       && vr1type == VR_ANTI_RANGE)
	{
	  /* If both are anti-ranges the result is the outer one.  */
	  *vr0type = vr1type;
	  *vr0min = vr1min;
	  *vr0max = vr1max;
	}
      else if (vr1type == VR_ANTI_RANGE
	       && *vr0type == VR_RANGE)
	{
	  /* The intersection is empty.  */
	  *vr0type = VR_UNDEFINED;
	  *vr0min = NULL_TREE;
	  *vr0max = NULL_TREE;
	}
      else
	gcc_unreachable ();
    }
  else if ((operand_less_p (vr1min, *vr0max) == 1
	    || operand_equal_p (vr1min, *vr0max, 0))
	   && operand_less_p (*vr0min, vr1min) == 1)
    {
      /* [  (  ]  ) or [  ](  ) */
      if (*vr0type == VR_ANTI_RANGE
	  && vr1type == VR_ANTI_RANGE)
	*vr0max = vr1max;
      else if (*vr0type == VR_RANGE
	       && vr1type == VR_RANGE)
	*vr0min = vr1min;
      else if (*vr0type == VR_RANGE
	       && vr1type == VR_ANTI_RANGE)
	{
	  if (TREE_CODE (vr1min) == INTEGER_CST)
	    *vr0max = int_const_binop (MINUS_EXPR, vr1min,
				       integer_one_node);
	  else
	    *vr0max = vr1min;
	}
      else if (*vr0type == VR_ANTI_RANGE
	       && vr1type == VR_RANGE)
	{
	  *vr0type = VR_RANGE;
	  if (TREE_CODE (*vr0max) == INTEGER_CST)
	    *vr0min = int_const_binop (PLUS_EXPR, *vr0max,
				       integer_one_node);
	  else
	    *vr0min = *vr0max;
	  *vr0max = vr1max;
	}
      else
	gcc_unreachable ();
    }
  else if ((operand_less_p (*vr0min, vr1max) == 1
	    || operand_equal_p (*vr0min, vr1max, 0))
	   && operand_less_p (vr1min, *vr0min) == 1)
    {
      /* (  [  )  ] or (  )[  ] */
      if (*vr0type == VR_ANTI_RANGE
	  && vr1type == VR_ANTI_RANGE)
	*vr0min = vr1min;
      else if (*vr0type == VR_RANGE
	       && vr1type == VR_RANGE)
	*vr0max = vr1max;
      else if (*vr0type == VR_RANGE
	       && vr1type == VR_ANTI_RANGE)
	{
	  if (TREE_CODE (vr1max) == INTEGER_CST)
	    *vr0min = int_const_binop (PLUS_EXPR, vr1max,
				       integer_one_node);
	  else
	    *vr0min = vr1max;
	}
      else if (*vr0type == VR_ANTI_RANGE
	       && vr1type == VR_RANGE)
	{
	  *vr0type = VR_RANGE;
	  if (TREE_CODE (*vr0min) == INTEGER_CST)
	    *vr0max = int_const_binop (MINUS_EXPR, *vr0min,
				       integer_one_node);
	  else
	    *vr0max = *vr0min;
	  *vr0min = vr1min;
	}
      else
	gcc_unreachable ();
    }

  /* As a fallback simply use { *VRTYPE, *VR0MIN, *VR0MAX } as
     result for the intersection.  That's always a conservative
     correct estimate.  */

  return;
}


/* Intersect the two value-ranges *VR0 and *VR1 and store the result
   in *VR0.  This may not be the smallest possible such range.  */

static void
vrp_intersect_ranges_1 (value_range_t *vr0, value_range_t *vr1)
{
  value_range_t saved;

  /* If either range is VR_VARYING the other one wins.  */
  if (vr1->type == VR_VARYING)
    return;
  if (vr0->type == VR_VARYING)
    {
      copy_value_range (vr0, vr1);
      return;
    }

  /* When either range is VR_UNDEFINED the resulting range is
     VR_UNDEFINED, too.  */
  if (vr0->type == VR_UNDEFINED)
    return;
  if (vr1->type == VR_UNDEFINED)
    {
      set_value_range_to_undefined (vr0);
      return;
    }

  /* Save the original vr0 so we can return it as conservative intersection
     result when our worker turns things to varying.  */
  saved = *vr0;
  intersect_ranges (&vr0->type, &vr0->min, &vr0->max,
		    vr1->type, vr1->min, vr1->max);
  /* Make sure to canonicalize the result though as the inversion of a
     VR_RANGE can still be a VR_RANGE.  */
  set_and_canonicalize_value_range (vr0, vr0->type,
				    vr0->min, vr0->max, vr0->equiv);
  /* If that failed, use the saved original VR0.  */
  if (vr0->type == VR_VARYING)
    {
      *vr0 = saved;
      return;
    }
  /* If the result is VR_UNDEFINED there is no need to mess with
     the equivalencies.  */
  if (vr0->type == VR_UNDEFINED)
    return;

  /* The resulting set of equivalences for range intersection is the union of
     the two sets.  */
  if (vr0->equiv && vr1->equiv && vr0->equiv != vr1->equiv)
    bitmap_ior_into (vr0->equiv, vr1->equiv);
  else if (vr1->equiv && !vr0->equiv)
    bitmap_copy (vr0->equiv, vr1->equiv);
}

static void
vrp_intersect_ranges (value_range_t *vr0, value_range_t *vr1)
{
  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "Intersecting\n  ");
      dump_value_range (dump_file, vr0);
      fprintf (dump_file, "\nand\n  ");
      dump_value_range (dump_file, vr1);
      fprintf (dump_file, "\n");
    }
  vrp_intersect_ranges_1 (vr0, vr1);
  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "to\n  ");
      dump_value_range (dump_file, vr0);
      fprintf (dump_file, "\n");
    }
}

/* Meet operation for value ranges.  Given two value ranges VR0 and
   VR1, store in VR0 a range that contains both VR0 and VR1.  This
   may not be the smallest possible such range.  */

static void
vrp_meet_1 (value_range_t *vr0, value_range_t *vr1)
{
  value_range_t saved;

  if (vr0->type == VR_UNDEFINED)
    {
      /* Drop equivalences.  See PR53465.  */
      set_value_range (vr0, vr1->type, vr1->min, vr1->max, NULL);
      return;
    }

  if (vr1->type == VR_UNDEFINED)
    {
      /* VR0 already has the resulting range, just drop equivalences.
	 See PR53465.  */
      if (vr0->equiv)
	bitmap_clear (vr0->equiv);
      return;
    }

  if (vr0->type == VR_VARYING)
    {
      /* Nothing to do.  VR0 already has the resulting range.  */
      return;
    }

  if (vr1->type == VR_VARYING)
    {
      set_value_range_to_varying (vr0);
      return;
    }

  saved = *vr0;
  union_ranges (&vr0->type, &vr0->min, &vr0->max,
		vr1->type, vr1->min, vr1->max);
  if (vr0->type == VR_VARYING)
    {
      /* Failed to find an efficient meet.  Before giving up and setting
	 the result to VARYING, see if we can at least derive a useful
	 anti-range.  FIXME, all this nonsense about distinguishing
	 anti-ranges from ranges is necessary because of the odd
	 semantics of range_includes_zero_p and friends.  */
      if (((saved.type == VR_RANGE
	    && range_includes_zero_p (saved.min, saved.max) == 0)
	   || (saved.type == VR_ANTI_RANGE
	       && range_includes_zero_p (saved.min, saved.max) == 1))
	  && ((vr1->type == VR_RANGE
	       && range_includes_zero_p (vr1->min, vr1->max) == 0)
	      || (vr1->type == VR_ANTI_RANGE
		  && range_includes_zero_p (vr1->min, vr1->max) == 1)))
	{
	  set_value_range_to_nonnull (vr0, TREE_TYPE (saved.min));

	  /* Since this meet operation did not result from the meeting of
	     two equivalent names, VR0 cannot have any equivalences.  */
	  if (vr0->equiv)
	    bitmap_clear (vr0->equiv);
	  return;
	}

      set_value_range_to_varying (vr0);
      return;
    }
  set_and_canonicalize_value_range (vr0, vr0->type, vr0->min, vr0->max,
				    vr0->equiv);
  if (vr0->type == VR_VARYING)
    return;

  /* The resulting set of equivalences is always the intersection of
     the two sets.  */
  if (vr0->equiv && vr1->equiv && vr0->equiv != vr1->equiv)
    bitmap_and_into (vr0->equiv, vr1->equiv);
  else if (vr0->equiv && !vr1->equiv)
    bitmap_clear (vr0->equiv);
}

static void
vrp_meet (value_range_t *vr0, value_range_t *vr1)
{
  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "Meeting\n  ");
      dump_value_range (dump_file, vr0);
      fprintf (dump_file, "\nand\n  ");
      dump_value_range (dump_file, vr1);
      fprintf (dump_file, "\n");
    }
  vrp_meet_1 (vr0, vr1);
  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "to\n  ");
      dump_value_range (dump_file, vr0);
      fprintf (dump_file, "\n");
    }
}


/* Visit all arguments for PHI node PHI that flow through executable
   edges.  If a valid value range can be derived from all the incoming
   value ranges, set a new range for the LHS of PHI.  */

static enum ssa_prop_result
vrp_visit_phi_node (GIMPLE_type phi)
{
  size_t i;
  tree lhs = PHI_RESULT (phi);
  value_range_t *lhs_vr = get_value_range (lhs);
  value_range_t vr_result = VR_INITIALIZER;
  bool first = true;
  int edges, old_edges;
  struct loop *l;

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "\nVisiting PHI node: ");
      print_gimple_stmt (dump_file, phi, 0, dump_flags);
    }

  edges = 0;
  for (i = 0; i < gimple_phi_num_args (phi); i++)
    {
      edge e = gimple_phi_arg_edge (phi, i);

      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file,
	      "\n    Argument #%d (%d -> %d %sexecutable)\n",
	      (int) i, e->src->index, e->dest->index,
	      (e->flags & EDGE_EXECUTABLE) ? "" : "not ");
	}

      if (e->flags & EDGE_EXECUTABLE)
	{
	  tree arg = PHI_ARG_DEF (phi, i);
	  value_range_t vr_arg;

	  ++edges;

	  if (TREE_CODE (arg) == SSA_NAME)
	    {
	      vr_arg = *(get_value_range (arg));
	    }
	  else
	    {
	      if (is_overflow_infinity (arg))
		{
		  arg = copy_node (arg);
		  TREE_OVERFLOW (arg) = 0;
		}

	      vr_arg.type = VR_RANGE;
	      vr_arg.min = arg;
	      vr_arg.max = arg;
	      vr_arg.equiv = NULL;
	    }

	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      fprintf (dump_file, "\t");
	      print_generic_expr (dump_file, arg, dump_flags);
	      fprintf (dump_file, "\n\tValue: ");
	      dump_value_range (dump_file, &vr_arg);
	      fprintf (dump_file, "\n");
	    }

	  if (first)
	    copy_value_range (&vr_result, &vr_arg);
	  else
	    vrp_meet (&vr_result, &vr_arg);
	  first = false;

	  if (vr_result.type == VR_VARYING)
	    break;
	}
    }

  if (vr_result.type == VR_VARYING)
    goto varying;
  else if (vr_result.type == VR_UNDEFINED)
    goto update_range;

  old_edges = vr_phi_edge_counts[SSA_NAME_VERSION (lhs)];
  vr_phi_edge_counts[SSA_NAME_VERSION (lhs)] = edges;

  /* To prevent infinite iterations in the algorithm, derive ranges
     when the new value is slightly bigger or smaller than the
     previous one.  We don't do this if we have seen a new executable
     edge; this helps us avoid an overflow infinity for conditionals
     which are not in a loop.  If the old value-range was VR_UNDEFINED
     use the updated range and iterate one more time.  */
  if (edges > 0
      && gimple_phi_num_args (phi) > 1
      && edges == old_edges
      && lhs_vr->type != VR_UNDEFINED)
    {
      int cmp_min = compare_values (lhs_vr->min, vr_result.min);
      int cmp_max = compare_values (lhs_vr->max, vr_result.max);

      /* For non VR_RANGE or for pointers fall back to varying if
	 the range changed.  */
      if ((lhs_vr->type != VR_RANGE || vr_result.type != VR_RANGE
	   || POINTER_TYPE_P (TREE_TYPE (lhs)))
	  && (cmp_min != 0 || cmp_max != 0))
	goto varying;

      /* If the new minimum is smaller or larger than the previous
	 one, go all the way to -INF.  In the first case, to avoid
	 iterating millions of times to reach -INF, and in the
	 other case to avoid infinite bouncing between different
	 minimums.  */
      if (cmp_min > 0 || cmp_min < 0)
	{
	  if (!needs_overflow_infinity (TREE_TYPE (vr_result.min))
	      || !vrp_var_may_overflow (lhs, phi))
	    vr_result.min = TYPE_MIN_VALUE (TREE_TYPE (vr_result.min));
	  else if (supports_overflow_infinity (TREE_TYPE (vr_result.min)))
	    vr_result.min =
		negative_overflow_infinity (TREE_TYPE (vr_result.min));
	}

      /* Similarly, if the new maximum is smaller or larger than
	 the previous one, go all the way to +INF.  */
      if (cmp_max < 0 || cmp_max > 0)
	{
	  if (!needs_overflow_infinity (TREE_TYPE (vr_result.max))
	      || !vrp_var_may_overflow (lhs, phi))
	    vr_result.max = TYPE_MAX_VALUE (TREE_TYPE (vr_result.max));
	  else if (supports_overflow_infinity (TREE_TYPE (vr_result.max)))
	    vr_result.max =
		positive_overflow_infinity (TREE_TYPE (vr_result.max));
	}

      /* If we dropped either bound to +-INF then if this is a loop
	 PHI node SCEV may known more about its value-range.  */
      if ((cmp_min > 0 || cmp_min < 0
	   || cmp_max < 0 || cmp_max > 0)
	  && current_loops
	  && (l = loop_containing_stmt (phi))
	  && l->header == gimple_bb (phi))
	adjust_range_with_scev (&vr_result, l, phi, lhs);

      /* If we will end up with a (-INF, +INF) range, set it to
	 VARYING.  Same if the previous max value was invalid for
	 the type and we end up with vr_result.min > vr_result.max.  */
      if ((vrp_val_is_max (vr_result.max)
	   && vrp_val_is_min (vr_result.min))
	  || compare_values (vr_result.min,
			     vr_result.max) > 0)
	goto varying;
    }

  /* If the new range is different than the previous value, keep
     iterating.  */
update_range:
  if (update_value_range (lhs, &vr_result))
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file, "Found new range for ");
	  print_generic_expr (dump_file, lhs, 0);
	  fprintf (dump_file, ": ");
	  dump_value_range (dump_file, &vr_result);
	  fprintf (dump_file, "\n\n");
	}

      return SSA_PROP_INTERESTING;
    }

  /* Nothing changed, don't add outgoing edges.  */
  return SSA_PROP_NOT_INTERESTING;

  /* No match found.  Set the LHS to VARYING.  */
varying:
  set_value_range_to_varying (lhs_vr);
  return SSA_PROP_VARYING;
}

/* Simplify boolean operations if the source is known
   to be already a boolean.  */
static bool
simplify_truth_ops_using_ranges (gimple_stmt_iterator *gsi, GIMPLE_type stmt)
{
  enum tree_code rhs_code = gimple_assign_rhs_code (stmt);
  tree lhs, op0, op1;
  bool need_conversion;

  /* We handle only !=/== case here.  */
  gcc_assert (rhs_code == EQ_EXPR || rhs_code == NE_EXPR);

  op0 = gimple_assign_rhs1 (stmt);
  if (!op_with_boolean_value_range_p (op0))
    return false;

  op1 = gimple_assign_rhs2 (stmt);
  if (!op_with_boolean_value_range_p (op1))
    return false;

  /* Reduce number of cases to handle to NE_EXPR.  As there is no
     BIT_XNOR_EXPR we cannot replace A == B with a single statement.  */
  if (rhs_code == EQ_EXPR)
    {
      if (TREE_CODE (op1) == INTEGER_CST)
	op1 = int_const_binop (BIT_XOR_EXPR, op1, integer_one_node);
      else
	return false;
    }

  lhs = gimple_assign_lhs (stmt);
  need_conversion
    = !useless_type_conversion_p (TREE_TYPE (lhs), TREE_TYPE (op0));

  /* Make sure to not sign-extend a 1-bit 1 when converting the result.  */
  if (need_conversion
      && !TYPE_UNSIGNED (TREE_TYPE (op0))
      && TYPE_PRECISION (TREE_TYPE (op0)) == 1
      && TYPE_PRECISION (TREE_TYPE (lhs)) > 1)
    return false;

  /* For A != 0 we can substitute A itself.  */
  if (integer_zerop (op1))
    gimple_assign_set_rhs_with_ops (gsi,
				    need_conversion
				    ? NOP_EXPR : TREE_CODE (op0),
				    op0, NULL_TREE);
  /* For A != B we substitute A ^ B.  Either with conversion.  */
  else if (need_conversion)
    {
      tree tem = make_ssa_name (TREE_TYPE (op0), NULL);
      GIMPLE_type newop = gimple_build_assign_with_ops (BIT_XOR_EXPR, tem, op0, op1);
      gsi_insert_before (gsi, newop, GSI_SAME_STMT);
      gimple_assign_set_rhs_with_ops (gsi, NOP_EXPR, tem, NULL_TREE);
    }
  /* Or without.  */
  else
    gimple_assign_set_rhs_with_ops (gsi, BIT_XOR_EXPR, op0, op1);
  update_stmt (gsi_stmt (*gsi));

  return true;
}

/* Simplify a division or modulo operator to a right shift or
   bitwise and if the first operand is unsigned or is greater
   than zero and the second operand is an exact power of two.  */

static bool
simplify_div_or_mod_using_ranges (GIMPLE_type stmt)
{
  enum tree_code rhs_code = gimple_assign_rhs_code (stmt);
  tree val = NULL;
  tree op0 = gimple_assign_rhs1 (stmt);
  tree op1 = gimple_assign_rhs2 (stmt);
  value_range_t *vr = get_value_range (gimple_assign_rhs1 (stmt));

  if (TYPE_UNSIGNED (TREE_TYPE (op0)))
    {
      val = integer_one_node;
    }
  else
    {
      bool sop = false;

      val = compare_range_with_value (GE_EXPR, vr, integer_zero_node, &sop);

      if (val
	  && sop
	  && integer_onep (val)
	  && issue_strict_overflow_warning (WARN_STRICT_OVERFLOW_MISC))
	{
	  location_t location;

	  if (!gimple_has_location (stmt))
	    location = input_location;
	  else
	    location = gimple_location (stmt);
	  warning_at (location, OPT_Wstrict_overflow,
		      "assuming signed overflow does not occur when "
		      "simplifying %</%> or %<%%%> to %<>>%> or %<&%>");
	}
    }

  if (val && integer_onep (val))
    {
      tree t;

      if (rhs_code == TRUNC_DIV_EXPR)
	{
	  t = build_int_cst (integer_type_node, tree_log2 (op1));
	  gimple_assign_set_rhs_code (stmt, RSHIFT_EXPR);
	  gimple_assign_set_rhs1 (stmt, op0);
	  gimple_assign_set_rhs2 (stmt, t);
	}
      else
	{
	  t = build_int_cst (TREE_TYPE (op1), 1);
	  t = int_const_binop (MINUS_EXPR, op1, t);
	  t = fold_convert (TREE_TYPE (op0), t);

	  gimple_assign_set_rhs_code (stmt, BIT_AND_EXPR);
	  gimple_assign_set_rhs1 (stmt, op0);
	  gimple_assign_set_rhs2 (stmt, t);
	}

      update_stmt (stmt);
      return true;
    }

  return false;
}

/* If the operand to an ABS_EXPR is >= 0, then eliminate the
   ABS_EXPR.  If the operand is <= 0, then simplify the
   ABS_EXPR into a NEGATE_EXPR.  */

static bool
simplify_abs_using_ranges (GIMPLE_type stmt)
{
  tree val = NULL;
  tree op = gimple_assign_rhs1 (stmt);
  tree type = TREE_TYPE (op);
  value_range_t *vr = get_value_range (op);

  if (TYPE_UNSIGNED (type))
    {
      val = integer_zero_node;
    }
  else if (vr)
    {
      bool sop = false;

      val = compare_range_with_value (LE_EXPR, vr, integer_zero_node, &sop);
      if (!val)
	{
	  sop = false;
	  val = compare_range_with_value (GE_EXPR, vr, integer_zero_node,
					  &sop);

	  if (val)
	    {
	      if (integer_zerop (val))
		val = integer_one_node;
	      else if (integer_onep (val))
		val = integer_zero_node;
	    }
	}

      if (val
	  && (integer_onep (val) || integer_zerop (val)))
	{
	  if (sop && issue_strict_overflow_warning (WARN_STRICT_OVERFLOW_MISC))
	    {
	      location_t location;

	      if (!gimple_has_location (stmt))
		location = input_location;
	      else
		location = gimple_location (stmt);
	      warning_at (location, OPT_Wstrict_overflow,
			  "assuming signed overflow does not occur when "
			  "simplifying %<abs (X)%> to %<X%> or %<-X%>");
	    }

	  gimple_assign_set_rhs1 (stmt, op);
	  if (integer_onep (val))
	    gimple_assign_set_rhs_code (stmt, NEGATE_EXPR);
	  else
	    gimple_assign_set_rhs_code (stmt, SSA_NAME);
	  update_stmt (stmt);
	  return true;
	}
    }

  return false;
}

/* Optimize away redundant BIT_AND_EXPR and BIT_IOR_EXPR.
   If all the bits that are being cleared by & are already
   known to be zero from VR, or all the bits that are being
   set by | are already known to be one from VR, the bit
   operation is redundant.  */

static bool
simplify_bit_ops_using_ranges (gimple_stmt_iterator *gsi, GIMPLE_type stmt)
{
  tree op0 = gimple_assign_rhs1 (stmt);
  tree op1 = gimple_assign_rhs2 (stmt);
  tree op = NULL_TREE;
  value_range_t vr0 = VR_INITIALIZER;
  value_range_t vr1 = VR_INITIALIZER;
  double_int may_be_nonzero0, may_be_nonzero1;
  double_int must_be_nonzero0, must_be_nonzero1;
  double_int mask;

  if (TREE_CODE (op0) == SSA_NAME)
    vr0 = *(get_value_range (op0));
  else if (is_gimple_min_invariant (op0))
    set_value_range_to_value (&vr0, op0, NULL);
  else
    return false;

  if (TREE_CODE (op1) == SSA_NAME)
    vr1 = *(get_value_range (op1));
  else if (is_gimple_min_invariant (op1))
    set_value_range_to_value (&vr1, op1, NULL);
  else
    return false;

  if (!zero_nonzero_bits_from_vr (&vr0, &may_be_nonzero0, &must_be_nonzero0))
    return false;
  if (!zero_nonzero_bits_from_vr (&vr1, &may_be_nonzero1, &must_be_nonzero1))
    return false;

  switch (gimple_assign_rhs_code (stmt))
    {
    case BIT_AND_EXPR:
      mask = may_be_nonzero0.and_not (must_be_nonzero1);
      if (mask.is_zero ())
	{
	  op = op0;
	  break;
	}
      mask = may_be_nonzero1.and_not (must_be_nonzero0);
      if (mask.is_zero ())
	{
	  op = op1;
	  break;
	}
      break;
    case BIT_IOR_EXPR:
      mask = may_be_nonzero0.and_not (must_be_nonzero1);
      if (mask.is_zero ())
	{
	  op = op1;
	  break;
	}
      mask = may_be_nonzero1.and_not (must_be_nonzero0);
      if (mask.is_zero ())
	{
	  op = op0;
	  break;
	}
      break;
    default:
      gcc_unreachable ();
    }

  if (op == NULL_TREE)
    return false;

  gimple_assign_set_rhs_with_ops (gsi, TREE_CODE (op), op, NULL);
  update_stmt (gsi_stmt (*gsi));
  return true;
}

/* We are comparing trees OP0 and OP1 using COND_CODE.  OP0 has
   a known value range VR.

   If there is one and only one value which will satisfy the
   conditional, then return that value.  Else return NULL.  */

static tree
test_for_singularity (enum tree_code cond_code, tree op0,
		      tree op1, value_range_t *vr)
{
  tree min = NULL;
  tree max = NULL;

  /* Extract minimum/maximum values which satisfy the
     the conditional as it was written.  */
  if (cond_code == LE_EXPR || cond_code == LT_EXPR)
    {
      /* This should not be negative infinity; there is no overflow
	 here.  */
      min = TYPE_MIN_VALUE (TREE_TYPE (op0));

      max = op1;
      if (cond_code == LT_EXPR && !is_overflow_infinity (max))
	{
	  tree one = build_int_cst (TREE_TYPE (op0), 1);
	  max = fold_build2 (MINUS_EXPR, TREE_TYPE (op0), max, one);
	  if (EXPR_P (max))
	    TREE_NO_WARNING (max) = 1;
	}
    }
  else if (cond_code == GE_EXPR || cond_code == GT_EXPR)
    {
      /* This should not be positive infinity; there is no overflow
	 here.  */
      max = TYPE_MAX_VALUE (TREE_TYPE (op0));

      min = op1;
      if (cond_code == GT_EXPR && !is_overflow_infinity (min))
	{
	  tree one = build_int_cst (TREE_TYPE (op0), 1);
	  min = fold_build2 (PLUS_EXPR, TREE_TYPE (op0), min, one);
	  if (EXPR_P (min))
	    TREE_NO_WARNING (min) = 1;
	}
    }

  /* Now refine the minimum and maximum values using any
     value range information we have for op0.  */
  if (min && max)
    {
      if (compare_values (vr->min, min) == 1)
	min = vr->min;
      if (compare_values (vr->max, max) == -1)
	max = vr->max;

      /* If the new min/max values have converged to a single value,
	 then there is only one value which can satisfy the condition,
	 return that value.  */
      if (operand_equal_p (min, max, 0) && is_gimple_min_invariant (min))
	return min;
    }
  return NULL;
}

/* Simplify a conditional using a relational operator to an equality
   test if the range information indicates only one value can satisfy
   the original conditional.  */

static bool
simplify_cond_using_ranges (GIMPLE_type stmt)
{
  tree op0 = gimple_cond_lhs (stmt);
  tree op1 = gimple_cond_rhs (stmt);
  enum tree_code cond_code = gimple_cond_code (stmt);

  if (cond_code != NE_EXPR
      && cond_code != EQ_EXPR
      && TREE_CODE (op0) == SSA_NAME
      && INTEGRAL_TYPE_P (TREE_TYPE (op0))
      && is_gimple_min_invariant (op1))
    {
      value_range_t *vr = get_value_range (op0);

      /* If we have range information for OP0, then we might be
	 able to simplify this conditional. */
      if (vr->type == VR_RANGE)
	{
	  tree new_tree = test_for_singularity (cond_code, op0, op1, vr);

	  if (new_tree)
	    {
	      if (dump_file)
		{
		  fprintf (dump_file, "Simplified relational ");
		  print_gimple_stmt (dump_file, stmt, 0, 0);
		  fprintf (dump_file, " into ");
		}

	      gimple_cond_set_code (stmt, EQ_EXPR);
	      gimple_cond_set_lhs (stmt, op0);
	      gimple_cond_set_rhs (stmt, new_tree);

	      update_stmt (stmt);

	      if (dump_file)
		{
		  print_gimple_stmt (dump_file, stmt, 0, 0);
		  fprintf (dump_file, "\n");
		}

	      return true;
	    }

	  /* Try again after inverting the condition.  We only deal
	     with integral types here, so no need to worry about
	     issues with inverting FP comparisons.  */
	  cond_code = invert_tree_comparison (cond_code, false);
	  new_tree = test_for_singularity (cond_code, op0, op1, vr);

	  if (new_tree)
	    {
	      if (dump_file)
		{
		  fprintf (dump_file, "Simplified relational ");
		  print_gimple_stmt (dump_file, stmt, 0, 0);
		  fprintf (dump_file, " into ");
		}

	      gimple_cond_set_code (stmt, NE_EXPR);
	      gimple_cond_set_lhs (stmt, op0);
	      gimple_cond_set_rhs (stmt, new_tree);

	      update_stmt (stmt);

	      if (dump_file)
		{
		  print_gimple_stmt (dump_file, stmt, 0, 0);
		  fprintf (dump_file, "\n");
		}

	      return true;
	    }
	}
    }

  return false;
}

/* Simplify a switch statement using the value range of the switch
   argument.  */

static bool
simplify_switch_using_ranges (GIMPLE_type stmt)
{
  tree op = gimple_switch_index (stmt);
  value_range_t *vr;
  bool take_default;
  edge e;
  edge_iterator ei;
  size_t i = 0, j = 0, n, n2;
  tree vec2;
  switch_update su;
  size_t k = 1, l = 0;

  if (TREE_CODE (op) == SSA_NAME)
    {
      vr = get_value_range (op);

      /* We can only handle integer ranges.  */
      if ((vr->type != VR_RANGE
	   && vr->type != VR_ANTI_RANGE)
	  || symbolic_range_p (vr))
	return false;

      /* Find case label for min/max of the value range.  */
      take_default = !find_case_label_ranges (stmt, vr, &i, &j, &k, &l);
    }
  else if (TREE_CODE (op) == INTEGER_CST)
    {
      take_default = !find_case_label_index (stmt, 1, op, &i);
      if (take_default)
	{
	  i = 1;
	  j = 0;
	}
      else
	{
	  j = i;
	}
    }
  else
    return false;

  n = gimple_switch_num_labels (stmt);

  /* Bail out if this is just all edges taken.  */
  if (i == 1
      && j == n - 1
      && take_default)
    return false;

  /* Build a new vector of taken case labels.  */
  vec2 = make_tree_vec (j - i + 1 + l - k + 1 + (int)take_default);
  n2 = 0;

  /* Add the default edge, if necessary.  */
  if (take_default)
    TREE_VEC_ELT (vec2, n2++) = gimple_switch_default_label (stmt);

  for (; i <= j; ++i, ++n2)
    TREE_VEC_ELT (vec2, n2) = gimple_switch_label (stmt, i);

  for (; k <= l; ++k, ++n2)
    TREE_VEC_ELT (vec2, n2) = gimple_switch_label (stmt, k);

  /* Mark needed edges.  */
  for (i = 0; i < n2; ++i)
    {
      e = find_edge (gimple_bb (stmt),
		     label_to_block (CASE_LABEL (TREE_VEC_ELT (vec2, i))));
      e->aux = (void *)-1;
    }

  /* Queue not needed edges for later removal.  */
  FOR_EACH_EDGE (e, ei, gimple_bb (stmt)->succs)
    {
      if (e->aux == (void *)-1)
	{
	  e->aux = NULL;
	  continue;
	}

      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file, "removing unreachable case label\n");
	}
      to_remove_edges.safe_push (e);
      e->flags &= ~EDGE_EXECUTABLE;
    }

  /* And queue an update for the stmt.  */
  su.stmt = stmt;
  su.vec = vec2;
  to_update_switch_stmts.safe_push (su);
  return false;
}

/* Simplify an integral conversion from an SSA name in STMT.  */

static bool
simplify_conversion_using_ranges (GIMPLE_type stmt)
{
  tree innerop, middleop, finaltype;
  GIMPLE_type def_stmt;
  value_range_t *innervr;
  bool inner_unsigned_p, middle_unsigned_p, final_unsigned_p;
  unsigned inner_prec, middle_prec, final_prec;
  double_int innermin, innermed, innermax, middlemin, middlemed, middlemax;

  finaltype = TREE_TYPE (gimple_assign_lhs (stmt));
  if (!INTEGRAL_TYPE_P (finaltype))
    return false;
  middleop = gimple_assign_rhs1 (stmt);
  def_stmt = SSA_NAME_DEF_STMT (middleop);
  if (!is_gimple_assign (def_stmt)
      || !CONVERT_EXPR_CODE_P (gimple_assign_rhs_code (def_stmt)))
    return false;
  innerop = gimple_assign_rhs1 (def_stmt);
  if (TREE_CODE (innerop) != SSA_NAME)
    return false;

  /* Get the value-range of the inner operand.  */
  innervr = get_value_range (innerop);
  if (innervr->type != VR_RANGE
      || TREE_CODE (innervr->min) != INTEGER_CST
      || TREE_CODE (innervr->max) != INTEGER_CST)
    return false;

  /* Simulate the conversion chain to check if the result is equal if
     the middle conversion is removed.  */
  innermin = tree_to_double_int (innervr->min);
  innermax = tree_to_double_int (innervr->max);

  inner_prec = TYPE_PRECISION (TREE_TYPE (innerop));
  middle_prec = TYPE_PRECISION (TREE_TYPE (middleop));
  final_prec = TYPE_PRECISION (finaltype);

  /* If the first conversion is not injective, the second must not
     be widening.  */
  if ((innermax - innermin).ugt (double_int::mask (middle_prec))
      && middle_prec < final_prec)
    return false;
  /* We also want a medium value so that we can track the effect that
     narrowing conversions with sign change have.  */
  inner_unsigned_p = TYPE_UNSIGNED (TREE_TYPE (innerop));
  if (inner_unsigned_p)
    innermed = double_int::mask (inner_prec).lrshift (1, inner_prec);
  else
    innermed = double_int_zero;
  if (innermin.cmp (innermed, inner_unsigned_p) >= 0
      || innermed.cmp (innermax, inner_unsigned_p) >= 0)
    innermed = innermin;

  middle_unsigned_p = TYPE_UNSIGNED (TREE_TYPE (middleop));
  middlemin = innermin.ext (middle_prec, middle_unsigned_p);
  middlemed = innermed.ext (middle_prec, middle_unsigned_p);
  middlemax = innermax.ext (middle_prec, middle_unsigned_p);

  /* Require that the final conversion applied to both the original
     and the intermediate range produces the same result.  */
  final_unsigned_p = TYPE_UNSIGNED (finaltype);
  if (middlemin.ext (final_prec, final_unsigned_p)
	 != innermin.ext (final_prec, final_unsigned_p)
      || middlemed.ext (final_prec, final_unsigned_p)
	 != innermed.ext (final_prec, final_unsigned_p)
      || middlemax.ext (final_prec, final_unsigned_p)
	 != innermax.ext (final_prec, final_unsigned_p))
    return false;

  gimple_assign_set_rhs1 (stmt, innerop);
  update_stmt (stmt);
  return true;
}

/* Return whether the value range *VR fits in an integer type specified
   by PRECISION and UNSIGNED_P.  */

static bool
range_fits_type_p (value_range_t *vr, unsigned precision, bool unsigned_p)
{
  tree src_type;
  unsigned src_precision;
  double_int tem;

  /* We can only handle integral and pointer types.  */
  src_type = TREE_TYPE (vr->min);
  if (!INTEGRAL_TYPE_P (src_type)
      && !POINTER_TYPE_P (src_type))
    return false;

  /* An extension is always fine, so is an identity transform.  */
  src_precision = TYPE_PRECISION (TREE_TYPE (vr->min));
  if (src_precision < precision
      || (src_precision == precision
	  && TYPE_UNSIGNED (src_type) == unsigned_p))
    return true;

  /* Now we can only handle ranges with constant bounds.  */
  if (vr->type != VR_RANGE
      || TREE_CODE (vr->min) != INTEGER_CST
      || TREE_CODE (vr->max) != INTEGER_CST)
    return false;

  /* For precision-preserving sign-changes the MSB of the double-int
     has to be clear.  */
  if (src_precision == precision
      && (TREE_INT_CST_HIGH (vr->min) | TREE_INT_CST_HIGH (vr->max)) < 0)
    return false;

  /* Then we can perform the conversion on both ends and compare
     the result for equality.  */
  tem = tree_to_double_int (vr->min).ext (precision, unsigned_p);
  if (tree_to_double_int (vr->min) != tem)
    return false;
  tem = tree_to_double_int (vr->max).ext (precision, unsigned_p);
  if (tree_to_double_int (vr->max) != tem)
    return false;

  return true;
}

/* Simplify a conversion from integral SSA name to float in STMT.  */

static bool
simplify_float_conversion_using_ranges (gimple_stmt_iterator *gsi, GIMPLE_type stmt)
{
  tree rhs1 = gimple_assign_rhs1 (stmt);
  value_range_t *vr = get_value_range (rhs1);
  enum machine_mode fltmode = TYPE_MODE (TREE_TYPE (gimple_assign_lhs (stmt)));
  enum machine_mode mode;
  tree tem;
  GIMPLE_type conv;

  /* We can only handle constant ranges.  */
  if (vr->type != VR_RANGE
      || TREE_CODE (vr->min) != INTEGER_CST
      || TREE_CODE (vr->max) != INTEGER_CST)
    return false;

  /* First check if we can use a signed type in place of an unsigned.  */
  if (TYPE_UNSIGNED (TREE_TYPE (rhs1))
      && (can_float_p (fltmode, TYPE_MODE (TREE_TYPE (rhs1)), 0)
	  != CODE_FOR_nothing)
      && range_fits_type_p (vr, GET_MODE_PRECISION
			          (TYPE_MODE (TREE_TYPE (rhs1))), 0))
    mode = TYPE_MODE (TREE_TYPE (rhs1));
  /* If we can do the conversion in the current input mode do nothing.  */
  else if (can_float_p (fltmode, TYPE_MODE (TREE_TYPE (rhs1)),
			TYPE_UNSIGNED (TREE_TYPE (rhs1))))
    return false;
  /* Otherwise search for a mode we can use, starting from the narrowest
     integer mode available.  */
  else
    {
      mode = GET_CLASS_NARROWEST_MODE (MODE_INT);
      do
	{
	  /* If we cannot do a signed conversion to float from mode
	     or if the value-range does not fit in the signed type
	     try with a wider mode.  */
	  if (can_float_p (fltmode, mode, 0) != CODE_FOR_nothing
	      && range_fits_type_p (vr, GET_MODE_PRECISION (mode), 0))
	    break;

	  mode = GET_MODE_WIDER_MODE (mode);
	  /* But do not widen the input.  Instead leave that to the
	     optabs expansion code.  */
	  if (GET_MODE_PRECISION (mode) > TYPE_PRECISION (TREE_TYPE (rhs1)))
	    return false;
	}
      while (mode != VOIDmode);
      if (mode == VOIDmode)
	return false;
    }

  /* It works, insert a truncation or sign-change before the
     float conversion.  */
  tem = make_ssa_name (build_nonstandard_integer_type
			  (GET_MODE_PRECISION (mode), 0), NULL);
  conv = gimple_build_assign_with_ops (NOP_EXPR, tem, rhs1, NULL_TREE);
  gsi_insert_before (gsi, conv, GSI_SAME_STMT);
  gimple_assign_set_rhs1 (stmt, tem);
  update_stmt (stmt);

  return true;
}

/* Simplify STMT using ranges if possible.  */

static bool
simplify_stmt_using_ranges (gimple_stmt_iterator *gsi)
{
  GIMPLE_type stmt = gsi_stmt (*gsi);
  if (is_gimple_assign (stmt))
    {
      enum tree_code rhs_code = gimple_assign_rhs_code (stmt);
      tree rhs1 = gimple_assign_rhs1 (stmt);

      switch (rhs_code)
	{
	case EQ_EXPR:
	case NE_EXPR:
          /* Transform EQ_EXPR, NE_EXPR into BIT_XOR_EXPR or identity
	     if the RHS is zero or one, and the LHS are known to be boolean
	     values.  */
	  if (INTEGRAL_TYPE_P (TREE_TYPE (rhs1)))
	    return simplify_truth_ops_using_ranges (gsi, stmt);
	  break;

      /* Transform TRUNC_DIV_EXPR and TRUNC_MOD_EXPR into RSHIFT_EXPR
	 and BIT_AND_EXPR respectively if the first operand is greater
	 than zero and the second operand is an exact power of two.  */
	case TRUNC_DIV_EXPR:
	case TRUNC_MOD_EXPR:
	  if (INTEGRAL_TYPE_P (TREE_TYPE (rhs1))
	      && integer_pow2p (gimple_assign_rhs2 (stmt)))
	    return simplify_div_or_mod_using_ranges (stmt);
	  break;

      /* Transform ABS (X) into X or -X as appropriate.  */
	case ABS_EXPR:
	  if (TREE_CODE (rhs1) == SSA_NAME
	      && INTEGRAL_TYPE_P (TREE_TYPE (rhs1)))
	    return simplify_abs_using_ranges (stmt);
	  break;

	case BIT_AND_EXPR:
	case BIT_IOR_EXPR:
	  /* Optimize away BIT_AND_EXPR and BIT_IOR_EXPR
	     if all the bits being cleared are already cleared or
	     all the bits being set are already set.  */
	  if (INTEGRAL_TYPE_P (TREE_TYPE (rhs1)))
	    return simplify_bit_ops_using_ranges (gsi, stmt);
	  break;

	CASE_CONVERT:
	  if (TREE_CODE (rhs1) == SSA_NAME
	      && INTEGRAL_TYPE_P (TREE_TYPE (rhs1)))
	    return simplify_conversion_using_ranges (stmt);
	  break;

	case FLOAT_EXPR:
	  if (TREE_CODE (rhs1) == SSA_NAME
	      && INTEGRAL_TYPE_P (TREE_TYPE (rhs1)))
	    return simplify_float_conversion_using_ranges (gsi, stmt);
	  break;

	default:
	  break;
	}
    }
  else if (gimple_code (stmt) == GIMPLE_COND)
    return simplify_cond_using_ranges (stmt);
  else if (gimple_code (stmt) == GIMPLE_SWITCH)
    return simplify_switch_using_ranges (stmt);

  return false;
}

/* If the statement pointed by SI has a predicate whose value can be
   computed using the value range information computed by VRP, compute
   its value and return true.  Otherwise, return false.  */

static bool
fold_predicate_in (gimple_stmt_iterator *si)
{
  bool assignment_p = false;
  tree val;
  GIMPLE_type stmt = gsi_stmt (*si);

  if (is_gimple_assign (stmt)
      && TREE_CODE_CLASS (gimple_assign_rhs_code (stmt)) == tcc_comparison)
    {
      assignment_p = true;
      val = vrp_evaluate_conditional (gimple_assign_rhs_code (stmt),
				      gimple_assign_rhs1 (stmt),
				      gimple_assign_rhs2 (stmt),
				      stmt);
    }
  else if (gimple_code (stmt) == GIMPLE_COND)
    val = vrp_evaluate_conditional (gimple_cond_code (stmt),
				    gimple_cond_lhs (stmt),
				    gimple_cond_rhs (stmt),
				    stmt);
  else
    return false;

  if (val)
    {
      if (assignment_p)
        val = fold_convert (gimple_expr_type (stmt), val);

      if (dump_file)
	{
	  fprintf (dump_file, "Folding predicate ");
	  print_gimple_expr (dump_file, stmt, 0, 0);
	  fprintf (dump_file, " to ");
	  print_generic_expr (dump_file, val, 0);
	  fprintf (dump_file, "\n");
	}

      if (is_gimple_assign (stmt))
	gimple_assign_set_rhs_from_tree (si, val);
      else
	{
	  gcc_assert (gimple_code (stmt) == GIMPLE_COND);
	  if (integer_zerop (val))
	    gimple_cond_make_false (stmt);
	  else if (integer_onep (val))
	    gimple_cond_make_true (stmt);
	  else
	    gcc_unreachable ();
	}

      return true;
    }

  return false;
}

/* Callback for substitute_and_fold folding the stmt at *SI.  */

static bool
vrp_fold_stmt (gimple_stmt_iterator *si)
{
  if (fold_predicate_in (si))
    return true;

  return simplify_stmt_using_ranges (si);
}

/* Stack of dest,src equivalency pairs that need to be restored after
   each attempt to thread a block's incoming edge to an outgoing edge.

   A NULL entry is used to mark the end of pairs which need to be
   restored.  */
static vec<tree> equiv_stack;

/* A trivial wrapper so that we can present the generic jump threading
   code with a simple API for simplifying statements.  STMT is the
   statement we want to simplify, WITHIN_STMT provides the location
   for any overflow warnings.  */

static tree
simplify_stmt_for_jump_threading (GIMPLE_type stmt, GIMPLE_type within_stmt)
{
  /* We only use VRP information to simplify conditionals.  This is
     overly conservative, but it's unclear if doing more would be
     worth the compile time cost.  */
  if (gimple_code (stmt) != GIMPLE_COND)
    return NULL;

  return vrp_evaluate_conditional (gimple_cond_code (stmt),
				   gimple_cond_lhs (stmt),
				   gimple_cond_rhs (stmt), within_stmt);
}

/* Blocks which have more than one predecessor and more than
   one successor present jump threading opportunities, i.e.,
   when the block is reached from a specific predecessor, we
   may be able to determine which of the outgoing edges will
   be traversed.  When this optimization applies, we are able
   to avoid conditionals at runtime and we may expose secondary
   optimization opportunities.

   This routine is effectively a driver for the generic jump
   threading code.  It basically just presents the generic code
   with edges that may be suitable for jump threading.

   Unlike DOM, we do not iterate VRP if jump threading was successful.
   While iterating may expose new opportunities for VRP, it is expected
   those opportunities would be very limited and the compile time cost
   to expose those opportunities would be significant.

   As jump threading opportunities are discovered, they are registered
   for later realization.  */

static void
identify_jump_threads (void)
{
  basic_block bb;
  GIMPLE_type dummy;
  int i;
  edge e;

  /* Ugh.  When substituting values earlier in this pass we can
     wipe the dominance information.  So rebuild the dominator
     information as we need it within the jump threading code.  */
  calculate_dominance_info (CDI_DOMINATORS);

  /* We do not allow VRP information to be used for jump threading
     across a back edge in the CFG.  Otherwise it becomes too
     difficult to avoid eliminating loop exit tests.  Of course
     EDGE_DFS_BACK is not accurate at this time so we have to
     recompute it.  */
  mark_dfs_back_edges ();

  /* Do not thread across edges we are about to remove.  Just marking
     them as EDGE_DFS_BACK will do.  */
  FOR_EACH_VEC_ELT (to_remove_edges, i, e)
    e->flags |= EDGE_DFS_BACK;

  /* Allocate our unwinder stack to unwind any temporary equivalences
     that might be recorded.  */
  equiv_stack.create (20);

  /* To avoid lots of silly node creation, we create a single
     conditional and just modify it in-place when attempting to
     thread jumps.  */
  dummy = gimple_build_cond (EQ_EXPR,
			     integer_zero_node, integer_zero_node,
			     NULL, NULL);

  /* Walk through all the blocks finding those which present a
     potential jump threading opportunity.  We could set this up
     as a dominator walker and record data during the walk, but
     I doubt it's worth the effort for the classes of jump
     threading opportunities we are trying to identify at this
     point in compilation.  */
  FOR_EACH_BB (bb)
    {
      GIMPLE_type last;

      /* If the generic jump threading code does not find this block
	 interesting, then there is nothing to do.  */
      if (! potentially_threadable_block (bb))
	continue;

      /* We only care about blocks ending in a COND_EXPR.  While there
	 may be some value in handling SWITCH_EXPR here, I doubt it's
	 terribly important.  */
      last = gsi_stmt (gsi_last_bb (bb));

      /* We're basically looking for a switch or any kind of conditional with
	 integral or pointer type arguments.  Note the type of the second
	 argument will be the same as the first argument, so no need to
	 check it explicitly.  */
      if (gimple_code (last) == GIMPLE_SWITCH
	  || (gimple_code (last) == GIMPLE_COND
      	      && TREE_CODE (gimple_cond_lhs (last)) == SSA_NAME
	      && (INTEGRAL_TYPE_P (TREE_TYPE (gimple_cond_lhs (last)))
		  || POINTER_TYPE_P (TREE_TYPE (gimple_cond_lhs (last))))
	      && (TREE_CODE (gimple_cond_rhs (last)) == SSA_NAME
		  || is_gimple_min_invariant (gimple_cond_rhs (last)))))
	{
	  edge_iterator ei;

	  /* We've got a block with multiple predecessors and multiple
	     successors which also ends in a suitable conditional or
	     switch statement.  For each predecessor, see if we can thread
	     it to a specific successor.  */
	  FOR_EACH_EDGE (e, ei, bb->preds)
	    {
	      /* Do not thread across back edges or abnormal edges
		 in the CFG.  */
	      if (e->flags & (EDGE_DFS_BACK | EDGE_COMPLEX))
		continue;

	      thread_across_edge (dummy, e, true, &equiv_stack,
				  simplify_stmt_for_jump_threading);
	    }
	}
    }

  /* We do not actually update the CFG or SSA graphs at this point as
     ASSERT_EXPRs are still in the IL and cfg cleanup code does not yet
     handle ASSERT_EXPRs gracefully.  */
}

/* We identified all the jump threading opportunities earlier, but could
   not transform the CFG at that time.  This routine transforms the
   CFG and arranges for the dominator tree to be rebuilt if necessary.

   Note the SSA graph update will occur during the normal TODO
   processing by the pass manager.  */
static void
finalize_jump_threads (void)
{
  thread_through_all_blocks (false);
  equiv_stack.release ();
}


#endif


/* Traverse all the blocks folding conditionals with known ranges.  */

static void
vrp_finalize_nofold (void)
{
  size_t i;
  tree new_ssa_var;

  values_propagated = true;
  
  debug_all_value_ranges();

  if (dump_file)
    {
      fprintf (dump_file, "\nValue ranges after VRP:\n\n");
      dump_all_value_ranges (dump_file);
      fprintf (dump_file, "\n");
    }

  //substitute_and_fold (op_with_constant_singleton_value_range,
  //    	       vrp_fold_stmt, false);


  if (warn_array_bounds)
    check_all_array_refs ();

  /* We must identify jump threading opportunities before we release
     the datastructures built by VRP.  */
  //identify_jump_threads ();

  /* Free allocated memory.  */
  for (i = 0; i < num_vr_values; i++)
    if (vr_value[i])
      {
	BITMAP_FREE (vr_value[i]->equiv);
	free (vr_value[i]);
      }

  free (vr_value);
  free (vr_phi_edge_counts);

  /* So that we can distinguish between VRP data being available
     and not available.  */
  vr_value = NULL;
  vr_phi_edge_counts = NULL;
}

static void restrict_range_to_consts()
{
  size_t i;
  for (i = 0; i < num_vr_values; i++)
    if (vr_value[i])
      {
        value_range_t *vr = vr_value[i];
        tree type = TREE_TYPE (ssa_name(i));
        tree minimum = NULL;
        tree maximum = NULL;
        unsigned var_prec = TYPE_PRECISION(type);

	//fprintf(stderr, "%ld\n", i);

        if (INTEGRAL_TYPE_P(type) && vr->min && vr->max)
	  {
	    bool is_neg_inf = is_negative_overflow_infinity (vr->min) || 
                 (INTEGRAL_TYPE_P (type)
	           && !TYPE_UNSIGNED (type)
	           && vrp_val_is_min (vr->min));
	           
	    bool is_pos_inf = is_positive_overflow_infinity (vr->max) || 
	        (INTEGRAL_TYPE_P (type)
	         && vrp_val_is_max (vr->max));
	    if(TREE_CODE (vr->min) != INTEGER_CST && !is_neg_inf)
	    {
	      /// check if greater than zero
	      bool strict_overflow_p;
	      tree val = compare_name_with_value(GE_EXPR, ssa_name(i), integer_zero_node, &strict_overflow_p);
	      if(!strict_overflow_p && val)
	      {
	        if(integer_onep (val))
	        {
	          minimum = integer_zero_node;
	        }
	        else
	        {
	          tree neg_const;
	          unsigned prec_index = 1;
	          while(prec_index < var_prec && !strict_overflow_p)
	          {
	            neg_const = build_int_cst (type, -(((unsigned HOST_WIDE_INT)1) << prec_index));
	            tree val = compare_name_with_value(GE_EXPR, ssa_name(i), neg_const, &strict_overflow_p);
	            if(val && integer_onep (val))
	            {
	              minimum = neg_const;
	              break;
	            }
	            ++prec_index;
	          }
	        }
	      }
	    } else if(is_neg_inf)
	      minimum = vr->min;
	    
	    if(TREE_CODE (vr->max) != INTEGER_CST && !is_pos_inf)
	    {
	      bool strict_overflow_p=false;
	      tree pos_const;
	      unsigned prec_index = 0;
	      while(prec_index < var_prec && !strict_overflow_p)
	      {
	        pos_const = build_int_cst (type, (((unsigned HOST_WIDE_INT)1) << prec_index));
	        tree val = compare_name_with_value(LT_EXPR, ssa_name(i), pos_const, &strict_overflow_p);
	        if(val && integer_onep (val))
	        {
	          maximum = build_int_cst (type, (((unsigned HOST_WIDE_INT)1) << prec_index)-1);
	          break;
	        }
	        ++prec_index;
	      }
	    }
	    else if(is_pos_inf)
	      maximum = vr->max;
	      
	    if(minimum)
	    {
	      vr->min = minimum;
	      vr->type = VR_RANGE;
	    }
	    if(maximum)
	    {
	      vr->max = maximum;
	      vr->type = VR_RANGE;
	    }
	  }
    }
  // do further restrictions by exploiting assert_expr
  for (i = 0; i < num_vr_values; i++)
    if (vr_value[i])
    {
      tree type = TREE_TYPE (ssa_name(i));
      value_range_t *vr = vr_value[i];
      if(INTEGRAL_TYPE_P(type) && vr->type == VR_RANGE && vr->min && vr->max)
      {
        tree sa_var = ssa_name(i);
        GIMPLE_type def_stmt = SSA_NAME_DEF_STMT (sa_var );
        if(is_gimple_assign (def_stmt)
	  && gimple_assign_rhs_code (def_stmt) == ASSERT_EXPR)
	{
	  tree src_var = ASSERT_EXPR_VAR (gimple_assign_rhs1 (def_stmt));
	  value_range_t *src_vr = vr_value[SSA_NAME_VERSION(src_var)];
	  if(src_vr && src_vr->type == VR_RANGE && src_vr->min && src_vr->max)
	  {
	    bool strict_overflow_p=false;
	    tree val = compare_name_with_value(LT_EXPR, src_var, vr->max, &strict_overflow_p);
	    if(val && integer_onep (val))
	      vr->max = src_vr->max;
	    strict_overflow_p=false;
	    val = compare_name_with_value(GT_EXPR, src_var, vr->min, &strict_overflow_p);
	    if(val && integer_onep (val))
	      vr->min = src_vr->min;
	  }
	}
      }
    }
}

extern unsigned int gimplePssaVRP (void);
extern unsigned int gimplePssa (void);

unsigned int
gimplePssaVRP (void)
{
  unsigned int res;
  
  //dump_flags = TDF_DETAILS;
  //dump_file = stderr;


  loop_optimizer_init (LOOPS_NORMAL | LOOPS_HAVE_RECORDED_EXITS);
  rewrite_into_loop_closed_ssa (NULL, TODO_update_ssa);
  scev_initialize ();

  insert_range_assertions ();
  
#if (GCC_VERSION < 4006)
  //estimate_numbers_of_iterations ();
  //scev_analysis();
#elif __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 8)
  /* Estimate number of iterations - but do not use undefined behavior
     for this.  We can't do this lazily as other functions may compute
     this using undefined behavior.  */
  free_numbers_of_iterations_estimates ();
  
  estimate_numbers_of_iterations ();
#else
  /* Estimate number of iterations - but do not use undefined behavior
     for this.  We can't do this lazily as other functions may compute
     this using undefined behavior.  */
  free_numbers_of_iterations_estimates ();
  
  estimate_numbers_of_iterations (false);
#endif
  
  vrp_initialize ();
  num_vr_values = num_ssa_names;

  ssa_propagate (vrp_visit_stmt, vrp_visit_phi_node);

  //debug_all_value_ranges();

  restrict_range_to_consts();
  
  // pass the ranges to gimplePssa
  set_num_vr_values(num_vr_values);
  set_vr_value(vr_value);
  
  res = gimplePssa();

  vrp_finalize_nofold();
  
  free_numbers_of_iterations_estimates ();

  remove_range_assertions ();
  
  /* If we exposed any new variables, go ahead and put them into
     SSA form now, before we handle jump threading.  This simplifies
     interactions between rewriting of _DECL nodes into SSA form
     and rewriting SSA_NAME nodes into SSA form after block
     duplication and CFG manipulation.  */
  update_ssa (TODO_update_ssa);
  
  scev_finalize ();
  loop_optimizer_finalize ();
  
  return res;
}

