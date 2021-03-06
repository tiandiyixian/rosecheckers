/*
 * Copyright (c) 2007-2012 Carnegie Mellon University.
 * 
 * All Rights Reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following acknowledgments
 * and disclaimers.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 * 
 * 3. The names “Carnegie Mellon University,” "CERT” and/or “Software
 * Engineering Institute" shall not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact
 * permission@sei.cmu.edu.
 * 
 * 4. Products derived from this software may not be called "CERT" nor
 * may "CERT" appear in their names without prior written permission
 * of permission@sei.cmu.edu.
 * 
 * 5. Redistributions of any form whatsoever must retain the following
 * acknowledgment:
 * 
 * "This product includes software developed by CERT with funding and
 * support from the Department of Defense under Contract No. FA
 * 8721-05-C-0003.  The U.S. Government's rights to use, modify,
 * reproduce, release, perform, display, or disclose this material are
 * restricted by the Rights in Technical Data-Noncommercial Items
 * clauses (DFAR 252-227.7013 and DFAR 252-227.7013 Alternate I
 * contained in the foregoing identified contract.
 * 
 * THIS SOFTWARE IS PROVIDED BY CARNEGIE MELLON UNIVERSITY “AS IS” AND
 * CARNEGIE MELLON UNIVERSITY MAKES NO WARRANTIES OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AS TO ANY MATTER, AND ALL SUCH WARRANTIES,
 * INCLUDING WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE, ARE EXPRESSLY DISCLAIMED.  WITHOUT LIMITING THE
 * GENERALITY OF THE FOREGOING, CARNEGIE MELLON UNIVERSITY DOES NOT
 * MAKE ANY WARRANTY OF ANY KIND RELATING TO EXCLUSIVITY,
 * INFORMATIONAL CONTENT, ERROR-FREE OPERATION, RESULTS TO BE OBTAINED
 * FROM USE, FREEDOM FROM PATENT, TRADEMARK AND COPYRIGHT INFRINGEMENT
 *  AND/OR FREEDOM FROM THEFT OF TRADE SECRETS.”
 */

/**
 * \file FLP.C
 */

#include <list>
#include <string>
#include <vector>
#include <algorithm>
#include "rose.h"
#include "utilities.h"

/**
 * Consider avoiding floating-point numbers when precise computation is needed
 */
bool FLP02_C(const SgNode *node ) {
	const SgBinaryOp *op = isSgBinaryOp(node);
	if (!op)
		return false;
	if (!(isSgEqualityOp(op)
		||isSgNotEqualOp(op)))
		return false;

	const SgExpression *lhs = op->get_lhs_operand();
	assert(lhs);
	const SgExpression *rhs = op->get_rhs_operand();
	assert(rhs);

	if (lhs->get_type()->isFloatType()
	  ||rhs->get_type()->isFloatType()) {
		if (isZeroVal(removeCasts(lhs))
		||  isZeroVal(removeCasts(rhs)))
			return false;
		
		print_error(node, "FLP02-C", "Consider avoiding floating-point numbers when precise computation is needed", true);
		return true;
	}
	return false;
}

/**
 * Detect and handle floating-point errors
 *
 * \bug ROSE can't handle the FENV_ACCESS pragma :(so there's no way to a
 * have a compliant solution here
 */
bool FLP03_C(const SgNode *node ) {
	if (isCompilerGeneratedNode(node))
		return false;

	/**
	 * We are looking for floating-point mult, divide (with non const/value on
	 * rhs), and floating-point casts from double->float
	 */
	if (isSgBinaryOp(node)) {
		const SgBinaryOp *op = isSgBinaryOp(node);
		assert(op);
		const SgType *lhsT = removeCasts(op->get_lhs_operand())->get_type();
		const SgType *rhsT = removeCasts(op->get_rhs_operand())->get_type();
		if (!(lhsT->isFloatType() || rhsT->isFloatType()))
			return false;
		bool lhsKnown = isSgValueExp(removeCasts(op->get_lhs_operand())) || isConstType(lhsT);
		bool rhsKnown = isSgValueExp(removeCasts(op->get_rhs_operand())) || isConstType(rhsT);
		if (isSgDivideOp(op)) {
			if (rhsKnown)
				return false;

			/** Also check for a divde-by-zero check on the previous line */
			const SgStatement *prevSt = findInBlockByOffset(node, -1);
			const SgVarRefExp *varRef = isSgVarRefExp(removeCasts(op->get_rhs_operand()));
			if (prevSt && varRef) {
				FOREACH_SUBNODE(prevSt, nodes, i, V_SgStatement) {
					if (isCheckForZero(isSgStatement(*i), varRef))
					return false;
				}
			}
		} else if (isSgMultiplyOp(op)) {
			if (lhsKnown && rhsKnown)
				return false;
		} else {
			return false;
		}
	} else if (isSgCastExp(node)) {
		const SgCastExp *cast = isSgCastExp(node);
		assert(cast);
		const SgType *lhsT = cast->get_type();
		const SgType *rhsT = cast->get_operand()->get_type();
		/**
		 * Bail if we're not dealing with floating-point, or if the types are
		 * compatible
		 */
		if (!lhsT->isFloatType() || !rhsT->isFloatType())
			return false;
		if (!((isSgTypeDouble(lhsT) && isSgTypeFloat(rhsT))
			||(isSgTypeLongDouble(rhsT) && !isSgTypeLongDouble(lhsT))))
			return false;
	} else {
		return false;
	}

	const SgStatement *prevSt = findInBlockByOffset(node, -1);
	bool no_feclearexcept = true;
	if (prevSt) {
		FOREACH_SUBNODE(prevSt, nodes, i, V_SgFunctionRefExp) {
			if (isCallOfFunctionNamed(isSgFunctionRefExp(*i), "feclearexcept"))
				no_feclearexcept = false;
		}
	}

	const SgStatement *nextSt = findInBlockByOffset(node,  1);
	bool no_fetestexcept = true;
	if (nextSt) {
		FOREACH_SUBNODE(nextSt, nodes, i, V_SgFunctionRefExp) {
			if (isCallOfFunctionNamed(isSgFunctionRefExp(*i), "fetestexcept"))
				no_fetestexcept = false;
		}
	}

	if (no_feclearexcept || no_fetestexcept) {
		print_error(node, "FLP03-C", "Detect and handle floating-point errors", true);
		return true;
	}

	return false;
}

/**
 * Convert integers to floating-point for floating-point operations
 *
 * \see INT07_C
 */
bool FLP06_C(const SgNode *node ) {
	const SgBinaryOp *binOp = isSgBinaryOp(node);
	const SgInitializedName *var = isSgInitializedName(node);
	const SgType *lhsT;
	const SgType *rhsT;

	if (binOp) {
		/**
		 * \todo Does this rule even make sense for binary ops?
		 */
		/**
		 * This should allow macros like isnan
		 */
		if (isCompilerGeneratedNode(binOp))
			return false;

		if (!binOp->get_type()->isFloatType())
			return false;
		if (isSgPntrArrRefExp(binOp))
			return false;

		lhsT = binOp->get_lhs_operand()->get_type()->stripTypedefsAndModifiers();
		/**
		 * Let's leave those casts in place to allow for macros like isnan
		 */
		rhsT = binOp->get_rhs_operand()->get_type()->stripTypedefsAndModifiers();
//		rhsT = removeImplicitPromotions(binOp->get_rhs_operand())->get_type();
		assert(lhsT);
		assert(rhsT);
		if (lhsT->isFloatType() || rhsT->isFloatType()) {
			return false;
		}
	} else if (var) {
		lhsT = var->get_type()->stripTypedefsAndModifiers();
		const SgAssignInitializer *init = isSgAssignInitializer(var->get_initializer());
		if (!init)
			return false;
		rhsT = removeImplicitPromotions(init->get_operand())->get_type()->stripTypedefsAndModifiers();

		assert(lhsT);
		assert(rhsT);
		if (!(lhsT->isFloatType() && rhsT->isIntegerType()))
			return false;
	} else
		return false;


	print_error(node, "FLP06-C", "Convert integers to floating-point for floating-point operations");
	return true;
}

/**
 * Do not use floating-point variables as loop counters
 */
bool FLP30_C(const SgNode *node ) {
	const SgForStatement *forSt = isSgForStatement(node);
	if (!forSt)
		return false;

	/* Check test */
	FOREACH_SUBNODE(forSt->get_test(), nodes, i, V_SgVarRefExp) {
		if (isSgVarRefExp(*i)->get_type()->isFloatType()) {
			print_error(*i, "FLP30-C", "Do not use floating-point variables as loop counters");
			return true;
		}
	}

	/* Check increment */
	FOREACH_SUBNODE(forSt->get_increment(), nodes2, i2, V_SgVarRefExp) {
		if (isSgVarRefExp(*i2)->get_type()->isFloatType()) {
			print_error(*i2, "FLP30-C", "Do not use floating-point variables as loop counters");
			return true;
		}
	}

	return false;
}


/**
 * Ensure that floating-point conversions are within range of the new type
 */
bool FLP34_C(const SgNode *node ) {
	/**
	 * \bug We really shouldn't have this cop-out here, but a lot of macros
	 * (ie. isnan) will trigger this rule otherwise
	 */
	if (isCompilerGeneratedNode(node))
		return false;

	const SgType *lhsType = NULL;
	const SgExpression *rhs = NULL;
	if (const SgBinaryOp *op = isSgBinaryOp(node)) {
		if (!isAnyAssignOp(op))
			return false;
		lhsType = op->get_lhs_operand()->get_type()->stripTypedefsAndModifiers();
		rhs = op->get_rhs_operand();
	} else if (const SgCastExp *cast = isSgCastExp(node)) {
		lhsType = cast->get_type()->stripTypedefsAndModifiers();
		rhs = cast->get_operand();
	} else {
		return false;
	}
	assert(lhsType && rhs);
	const SgType *rhsType = rhs->get_type()->stripTypedefsAndModifiers();
	assert(rhsType);
	if (!rhsType->isFloatType())
		return false;
	if (lhsType->isFloatType() && (sizeOfType(lhsType) >= sizeOfType(rhsType)))
		return false;

	if (valueVerified(removeCasts(rhs)))
		return false;

	print_error(node, "FLP34-C", "Ensure that floating-point conversions are within range of the new type");
	return true;
}

bool FLP_C(const SgNode *node) {
  bool violation = false;
  violation |= FLP02_C(node);
  violation |= FLP03_C(node);
  violation |= FLP06_C(node);
  violation |= FLP30_C(node);
  violation |= FLP34_C(node);
  return violation;
}


/// C++ checkers

bool FLP_CPP(const SgNode *node) {
  bool violation = false;
  violation |= FLP_C(node);
  return violation;
}

