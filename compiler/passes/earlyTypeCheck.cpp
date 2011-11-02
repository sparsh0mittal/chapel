#include "astutil.h"
#include "build.h"
#include "expr.h"
#include "stmt.h"
#include "passes.h"
#include "scopeResolve.h"
#include "stringutil.h"
#include "symbol.h"
#include "view.h"

//FIXME: Convert these over to the Chapel structures
#include <iostream>
#include <vector>
#include <list>
#include <string>
#include <utility>

//Predeclare recursive functions
BaseAST *get_root_type_or_type_expr(BaseAST *ast);
BaseAST *typeCheckExpr(BaseAST *currentExpr, BaseAST *expectedReturnTypeExpr);
void handle_where_clause_expr(BaseAST *ast);
BaseAST *typeCheckFn(FnSymbol *fn);
void checkInterfaceImplementations(BlockStmt *block);

//FIXME: This probably already exists in Chapel, but I couldn't find it
//FIXME: A more optimized approach would be to use the congruence closure to find all representative types at
//        the beginning of a compile, and keep the information in the AST instead of looking it up like this once and
//        then forgetting it in later passes
BaseAST *get_root_type_or_type_expr(BaseAST *ast) {
  BaseAST *retval;

  if (isType(ast)) {
    if ((ast == dtUnknown) || (ast == dtAny))
      return NULL;
    else
      return ast;
  }
  else if (VarSymbol *vs = toVarSymbol(ast)) {
    if (vs->type && (retval = get_root_type_or_type_expr(vs->type)))
      return retval;
    else
      return vs;
  }
  else if (TypeSymbol *ts = toTypeSymbol(ast)) {
    if (ts->type && (retval = get_root_type_or_type_expr(ts->type)))
      return retval;
    else
      return ts;
  }
  else if (ArgSymbol *as = toArgSymbol(ast)) {
    if (as->typeExpr)
      return get_root_type_or_type_expr(as->typeExpr);
    else if (as->type && get_root_type_or_type_expr(as->type))
      return as->type;
    else
      return as;
  }
  else if (Symbol *s = toSymbol(ast)) {
    return s;
  }
  else if (isExpr(ast)) {
    if (DefExpr *de = toDefExpr(ast)) {
      if (de->init) {
        return get_root_type_or_type_expr(de->init);
      }
      else if (de->exprType) {
        return get_root_type_or_type_expr(de->exprType);
      }
      else if (de->sym) {
        return get_root_type_or_type_expr(de->sym);
      }
    }
    else if (SymExpr *se = toSymExpr(ast)) {
      return get_root_type_or_type_expr(se->var);
    }
    else if (CallExpr *ce = toCallExpr(ast)) {
      return ce;
    }
    else if (BlockStmt *bs = toBlockStmt(ast)) {
      //FIXME: is this strictly correct?
      return get_root_type_or_type_expr(bs->body.head);
    }
    else if (isUnresolvedSymExpr(ast)) {
      return dtUnknown;
    }
    else {
      INT_FATAL("Unimplemented case in get_root_type_or_type_expr(expr): %i\n", toExpr(ast)->astTag);
    }
  }
  else {
    INT_FATAL("Unimplemented case in get_root_type_or_type_expr(ast)");
  }
  INT_FATAL("Unimplemented case in get_root_type_or_type_expr");
  return 0;
}

struct CCNode {
  CCNode* parent;

  BaseAST *actualExprOrType;

  int unique_id;

  std::vector<CCNode*> contains;
  std::vector<CCNode*> contained_by;

  CCNode() : parent(0), actualExprOrType(0) {}
};

typedef std::vector<std::pair<int, CCNode *> > CCNodeAssocList;
typedef std::vector<std::pair<int, CCNode *> >::iterator CCNodeAssocListIter;
typedef std::vector<std::pair<int, CCNode *> >::reverse_iterator CCNodeAssocListRIter;

struct CongruenceClosure {
  CCNodeAssocList node_assoc_list;
  std::vector<unsigned> scope_stops;

  BaseAST *get_representative_ast(BaseAST *ast) {
    CCNode *rep = representative(find_or_insert(ast));
    return rep->actualExprOrType;
  }

  CCNode *representative(CCNode *node) {
    while (node->parent)
      node = node->parent;

    return node;
  }

  void print_map() {
    for (CCNodeAssocListRIter i = node_assoc_list.rbegin(),
        e = node_assoc_list.rend(); i != e; ++i) {

      printf("%i[%p]: ", i->first, i->second);

      CCNode *cc = i->second;

      while (cc) {
        printf("%i[%p %p] ", cc->unique_id, cc, cc->parent);
        if (!cc->parent) {
          for (unsigned j = 0; j < cc->contains.size(); ++j) {
            printf("(%i)", cc->contains[j]->unique_id);
          }
        }
        cc = cc->parent;
      }
      printf("\n");
    }
  }

  CCNode *find_or_insert(BaseAST *ast) {
    //int unique_id = get_symbol_id(ast);
    if (BaseAST *rootAST = get_root_type_or_type_expr(ast))
      ast = rootAST;

    int unique_id = ast->id;

    std::vector<CCNode *> children_nodes;

    if (CallExpr *call = toCallExpr(ast)) {
      //Insert the baseExpr
      unique_id = representative(find_or_insert(call->baseExpr))->unique_id;

      //Collect all the children
      for_alist(arg, call->argList) {
        //FIXME: Not sure if this is strictly correct to use the representative here
        children_nodes.push_back(representative(find_or_insert(arg)));
      }
    }

    for (CCNodeAssocListRIter i = node_assoc_list.rbegin(),
        e = node_assoc_list.rend(); i != e; ++i) {
      if (unique_id == i->first) {
        CCNode *current_node = i->second;

        bool found_match = false;

        if (children_nodes.size() == current_node->contains.size()) {
          found_match = true;
          for (unsigned j = 0; j < children_nodes.size(); ++j) {
            if (children_nodes[j] != current_node->contains[j]) {
              found_match = false;
              break;
            }
          }
        }
        if (!found_match)
          continue;

        return current_node;
      }
    }

    // Not found, so we insert and return that
    CCNode *new_node = new CCNode();
    new_node->unique_id = unique_id;
    new_node->actualExprOrType = ast;
    for (unsigned i = 0; i < children_nodes.size(); ++i) {
      // Set up the bi-directional link between containing type and contained type
      children_nodes[i]->contained_by.push_back(new_node);
      new_node->contains.push_back(children_nodes[i]);
    }

    node_assoc_list.push_back(std::make_pair(unique_id, new_node));

    return new_node;
  }

  CCNode *get_node(int id) {
    for (CCNodeAssocListRIter i = node_assoc_list.rbegin(),
        e = node_assoc_list.rend(); i != e; ++i) {
      if (id == i->first) {
        return i->second;
      }
    }
    return (CCNode *)0;
  }

  bool is_equal(BaseAST *ast1, BaseAST *ast2) {
    CCNode *node1 = find_or_insert(ast1);
    CCNode *node2 = find_or_insert(ast2);

    return is_equal_helper(node1, node2);
  }

  bool is_equal_helper(CCNode *node1, CCNode *node2) {
    node1 = representative(node1);
    node2 = representative(node2);

    return node1 == node2;
  }

  void equate(BaseAST *ast1, BaseAST *ast2) {
    CCNode *node1 = find_or_insert(ast1);
    CCNode *node2 = find_or_insert(ast2);

    equate_helper(node1, node2);
  }
  void equate_helper(CCNode *node1, CCNode *node2) {
    CCNode *node1_tmp = representative(node1);
    CCNode *node2_tmp = representative(node2);

    if (node1_tmp == node2_tmp)
      return;

    // Union the two trees, favoring the earlier ids
    // this favors symbols over exprs, and types over symbols (I think, please check)
    if (node1_tmp->unique_id < node2_tmp->unique_id)
      node2_tmp->parent = node1_tmp;
    else
      node1_tmp->parent = node2_tmp;

    // Then, make sure that the "contained by" information is propagated to representative
    for (unsigned i = 0; i < node2->contained_by.size(); ++i) {
      node1_tmp->contained_by.push_back(node2->contained_by[i]);
    }

    // Do the rest of the closure.

    // Step #1, equate all contained nodes
    if (!node1->contains.empty() && !node2->contains.empty())
      INT_ASSERT(node1->contains.size() == node2->contains.size() && "Mismatched shape during union");

    if (node1->contains.size() == node2->contains.size()) {
      // if we have the same shape, we can pattern match and also union the contained nodes
      for (unsigned i = 0; i < node1->contains.size(); ++i) {
        equate_helper(node1->contains[i], node2->contains[i]);
      }
    }

    // Step #2, equate contained_by nodes which match by id
    if (!node1->contained_by.empty() && !node2->contained_by.empty()) {
      for (unsigned i = 0; i < node1->contained_by.size(); ++i) {
        for (unsigned j = 0; j < node2->contained_by.size(); ++j) {
          equate_if_children_match(node1->contained_by[i], node2->contained_by[j]);
        }
      }
    }
  }

  void equate_if_children_match(CCNode *node1, CCNode *node2) {
    if (representative(node1) == representative(node2))
      return;

    if (node1->unique_id != node2->unique_id)
      return;

    if (node1->contains.size() != node2->contains.size())
      return;

    for (unsigned i = 0; i < node1->contains.size(); ++i) {
      if (!is_equal_helper(node1->contains[i], node2->contains[i]))
        return;
    }

    equate_helper(node1, node2);
  }
};

CongruenceClosure cclosure;

/*
 * BEGIN DUPLICATE CODE
 * FIXME: Need to refactor once we know what we need here
 */

class VisibleFunctionBlock2 {
 public:
  Map<const char*,Vec<FnSymbol*>*> visibleFunctions;
  VisibleFunctionBlock2() { }
};

class CallInfo2 {
 public:
  CallExpr*        call;        // call expression
  FnSymbol*        interface;   // function interface
  BlockStmt*       scope;       // module scope as in M.call
  const char*      name;        // function name
  Vec<Symbol*>     actuals;     // actual symbols
  Vec<const char*> actualNames; // named arguments
  CallInfo2(CallExpr* icall);
  CallInfo2(FnSymbol* iface);
};

CallInfo2::CallInfo2(FnSymbol* fn) : interface(fn), scope(NULL) {
  name = fn->name;

  for_alist (formal, fn->formals) {
    printf("stuff");
  }
}

CallInfo2::CallInfo2(CallExpr* icall) : call(icall), scope(NULL) {
  if (SymExpr* se = toSymExpr(call->baseExpr))
    name = se->var->name;
  else if (UnresolvedSymExpr* use = toUnresolvedSymExpr(call->baseExpr))
    name = use->unresolved;
  if (call->numActuals() >= 2) {
    if (SymExpr* se = toSymExpr(call->get(1))) {
      if (se->var == gModuleToken) {
        se->remove();
        se = toSymExpr(call->get(1));
        INT_ASSERT(se);
        ModuleSymbol* mod = toModuleSymbol(se->var);
        INT_ASSERT(mod);
        se->remove();
        scope = mod->block;
      }
    }
  }
  for_actuals(actual, call) {
    if (NamedExpr* named = toNamedExpr(actual)) {
      actualNames.add(named->name);
      actual = named->actual;
    } else {
      actualNames.add(NULL);
    }
    SymExpr* se = toSymExpr(actual);
    INT_ASSERT(se);
    Type* t = se->var->type;
    if (t == dtUnknown || t->symbol->hasFlag(FLAG_GENERIC))
      INT_FATAL(call, "the type of the actual argument '%s' is unknown or generic", se->var->name);
    actuals.add(se->var);
  }
}

static Map<BlockStmt*,VisibleFunctionBlock2*> visibleFunctionMap;
static int nVisibleFunctions = 0; // for incremental build
static Map<BlockStmt*,BlockStmt*> visibilityBlockCache;
static Vec<BlockStmt*> standardModuleSet;

static BlockStmt*
getVisibleFunctions(BlockStmt* block,
                    const char* name,
                    Vec<FnSymbol*>& visibleFns,
                    Vec<BlockStmt*>& visited) {
  //
  // all functions in standard modules are stored in a single block
  //
  if (standardModuleSet.set_in(block))
    block = theProgram->block;

  //
  // avoid infinite recursion due to modules with mutual uses
  //
  if (visited.set_in(block))
    return NULL;
  else if (isModuleSymbol(block->parentSymbol))
    visited.set_add(block);

  bool canSkipThisBlock = true;

  VisibleFunctionBlock2* vfb = visibleFunctionMap.get(block);
  if (vfb) {
    canSkipThisBlock = false; // cannot skip if this block defines functions
    Vec<FnSymbol*>* fns = vfb->visibleFunctions.get(name);
    if (fns) {
      visibleFns.append(*fns);
    }
  }

  if (block->modUses) {
    for_actuals(expr, block->modUses) {
      SymExpr* se = toSymExpr(expr);
      INT_ASSERT(se);
      ModuleSymbol* mod = toModuleSymbol(se->var);
      INT_ASSERT(mod);
      canSkipThisBlock = false; // cannot skip if this block uses modules
      getVisibleFunctions(mod->block, name, visibleFns, visited);
    }
  }

  //
  // visibilityBlockCache contains blocks that can be skipped
  //
  if (BlockStmt* next = visibilityBlockCache.get(block)) {
    getVisibleFunctions(next, name, visibleFns, visited);
    return (canSkipThisBlock) ? next : block;
  }

  if (block != rootModule->block) {
    BlockStmt* next = getVisibilityBlock(block);
    BlockStmt* cache = getVisibleFunctions(next, name, visibleFns, visited);
    if (cache)
      visibilityBlockCache.put(block, cache);
    return (canSkipThisBlock) ? cache : block;
  }

  return NULL;
}

static void buildVisibleFunctionMap2() {
  for (int i = nVisibleFunctions; i < gFnSymbols.n; i++) {
    FnSymbol* fn = gFnSymbols.v[i];
    if (!fn->hasFlag(FLAG_INVISIBLE_FN) && fn->defPoint->parentSymbol &&
        !isArgSymbol(fn->defPoint->parentSymbol)) {
      BlockStmt* block = NULL;
      if (fn->hasFlag(FLAG_AUTO_II)) {
        block = theProgram->block;
      } else {
        block = getVisibilityBlock(fn->defPoint);
        //
        // add all functions in standard modules to theProgram
        //
        if (standardModuleSet.set_in(block))
          block = theProgram->block;
      }
      VisibleFunctionBlock2* vfb = visibleFunctionMap.get(block);
      if (!vfb) {
        vfb = new VisibleFunctionBlock2();
        visibleFunctionMap.put(block, vfb);
      }
      Vec<FnSymbol*>* fns = vfb->visibleFunctions.get(fn->name);
      if (!fns) {
        fns = new Vec<FnSymbol*>();
        vfb->visibleFunctions.put(fn->name, fns);
      }
      fns->add(fn);
    }
  }
  nVisibleFunctions = gFnSymbols.n;
}

/*
 * END DUPLICATE CODE
 */

// Typechecks the given ast node with the expected return (in case a return
// is encountered)
BaseAST *typeCheckExpr(BaseAST *currentExpr, BaseAST *expectedReturnTypeExpr) {
  if (SymExpr *se_actual = toSymExpr(currentExpr)) {

    if (se_actual->var->type && se_actual->var->type != dtUnknown)
      return se_actual->var->type;

    ArgSymbol *argSym;
    DefExpr *defPt;
    if ((argSym = toArgSymbol(se_actual->var)) && argSym->typeExpr) {
      return cclosure.get_representative_ast(argSym->typeExpr);
    }
    else if ((defPt = se_actual->var->defPoint)) {
      return typeCheckExpr(defPt, expectedReturnTypeExpr);
    }
  }
  else if (CallExpr* call = toCallExpr(currentExpr)) {
    Vec<Expr*> checked_arg_exprs;

    if (call->primitive && (call->primitive->tag == PRIM_RETURN)) {
      BaseAST *e_typeAST = typeCheckExpr(call->argList.head, expectedReturnTypeExpr);

      if (!cclosure.is_equal(e_typeAST, expectedReturnTypeExpr)) {
        INT_FATAL("Mismatched type expressions in return\n");
      }
      else {
        return e_typeAST;
      }
    }
    else if (call->primitive) {
      INT_FATAL("UNIMPLEMENTED: PRIMITIVE OP\n");
    }
    else if (!call->primitive) {
      //First, check to see if it should be a primitive but it hasn't been
      //resolved yet

      if (UnresolvedSymExpr *use = toUnresolvedSymExpr(call->baseExpr)) {
        if (PrimitiveOp *op = primitives_map.get(use->unresolved)) {
          if (call->argList.length == 0) {
            return op->returnInfo(call, NULL, NULL);
          }
          else if (call->argList.length == 1) {
            return op->returnInfo(call, cclosure.get_representative_ast(call->argList.get(1)), NULL);
          }
          else {
            return op->returnInfo(call, cclosure.get_representative_ast(call->argList.get(1)),
                cclosure.get_representative_ast(call->argList.get(2)));
          }
        }
      }

      CallInfo2 info(call);

      Vec<FnSymbol*> visibleFns;                    // visible functions

      if (gFnSymbols.n != nVisibleFunctions)
        buildVisibleFunctionMap2();

      if (!call->isResolved()) {
        if (!info.scope) {
          Vec<BlockStmt*> visited;
          getVisibleFunctions(getVisibilityBlock(call), info.name, visibleFns,
                              visited);
        } else {
          if (VisibleFunctionBlock2* vfb = visibleFunctionMap.get(info.scope))
            if (Vec<FnSymbol*>* fns = vfb->visibleFunctions.get(info.name))
              visibleFns.append(*fns);
        }
      } else {
        visibleFns.add(call->isResolved());
      }

      forv_Vec(FnSymbol, visibleFn, visibleFns) {
        bool mismatch = false;
        Expr *e_actual = call->argList.head;
        ArgSymbol *s_formal = ((visibleFn)->formals.head) ?
          toArgSymbol(toDefExpr((visibleFn)->formals.head)->sym) : NULL;
        for (; e_actual; e_actual = e_actual->next,
               s_formal = (s_formal && s_formal->defPoint->next) ?
               toArgSymbol(toDefExpr((s_formal)->defPoint->next)->sym) :
               NULL) {
          if (!s_formal) {
            mismatch = true;
            break;
          }
          if (!cclosure.is_equal(e_actual, s_formal->typeExpr)) {
            mismatch = true;
            break;
          }
        }
        if (!mismatch) {
          return typeCheckFn(visibleFn);
        }
      }
      INT_FATAL("No matching functions at call");
    }
  }
  else if (UnresolvedSymExpr *use = toUnresolvedSymExpr(currentExpr)) {
    INT_FATAL("UNIMPLEMENTED: Unresolved SymExpr: %s\n", use->unresolved);
  }
  else if (DefExpr *de = toDefExpr(currentExpr)) {
    if (de->exprType && de->init) {
      if (!cclosure.is_equal(de->exprType, de->init)) {
        INT_FATAL("Declared variable type does not match initialization expression\n");
      }
    }
    else if (de->exprType) {
      return de->exprType;
    }
    else if (de->init) {
      return de->init;
    }
    else {
      INT_FATAL("Generic variable without initializing expression\n");
    }
  }
  else if (NamedExpr *ne = toNamedExpr(currentExpr)) {
    return typeCheckExpr(ne->actual, expectedReturnTypeExpr);
  }
  else if (BlockStmt *block = toBlockStmt(currentExpr)) {
    BaseAST *ret = NULL;
    for_alist(e, block->body) {
      ret = typeCheckExpr(e, expectedReturnTypeExpr);
    }
    return ret;
  }
  else if (CondStmt *cond = toCondStmt(currentExpr)) {
    INT_FATAL("UNIMPLEMENTED: ContStmt\n");
    typeCheckExpr(cond->condExpr, expectedReturnTypeExpr);
    typeCheckExpr(cond->thenStmt, expectedReturnTypeExpr);
    typeCheckExpr(cond->elseStmt, expectedReturnTypeExpr);
  }
  else if (GotoStmt *gotoStmt = toGotoStmt(currentExpr)) {
    typeCheckExpr(gotoStmt->label, expectedReturnTypeExpr);
  }
  else {
    if (currentExpr) {
      INT_FATAL("UNIMPLEMENTED: <expr type unknown: %i %i %i %i>\n",
          currentExpr->astTag, E_Expr, E_Symbol, E_Type);
    }
    else {
      INT_FATAL("UNIMPLEMENTED: <expr is NULL\n");
    }
  }
  return NULL;
}

void handle_where_clause_expr(BaseAST *ast) {
  if (CallExpr *ce = toCallExpr(ast)) {
    if (UnresolvedSymExpr *callsymexpr = toUnresolvedSymExpr(ce->baseExpr)) {
      if (!strcmp(callsymexpr->unresolved, "==")) {
        //Equality constraint, let's add it
        BaseAST *arg1 = ce->argList.get(1);
        BaseAST *arg2 = ce->argList.get(2);
        cclosure.equate(arg1, arg2);
      }
      else if (!strcmp(callsymexpr->unresolved, "_build_tuple")) {
        //We have multiple constraints, handle them
        for_alist(arg, ce->argList) {
          handle_where_clause_expr(arg);
        }
      }
    }
    else {
    	printf("Not unresolved\n");
    }
  }
}

BaseAST *typeCheckFn(FnSymbol *fn) {
  // Look through our formal arguments
  for_formals(formal, fn) {
    if (!formal->typeExpr && (!formal->flags.test(FLAG_TYPE_VARIABLE))) {
      INT_FATAL("Formal is missing explicit type annotation\n");
    }
  }
  if (!fn->retExprType) {
    INT_FATAL("function missing explicit return type annotation\n");
  }
  
  if (fn->where) {
    for_alist(expr, fn->where->body) {
      handle_where_clause_expr(expr);
    }
  }

  // Now, look through the function body
  return typeCheckExpr(fn->body, fn->retExprType);
}

void checkInterfaceImplementations(BlockStmt *block) {
  for_alist(s, block->body) {
    if (DefExpr *de = toDefExpr(s)) {
      if (de->sym && isFnSymbol(de->sym)) {
        FnSymbol *fn = toFnSymbol(de->sym);
        checkInterfaceImplementations(fn->body);
      }
    }
    else if (CallExpr *ce = toCallExpr(s)) {
      if (UnresolvedSymExpr *use = toUnresolvedSymExpr(ce->baseExpr)) {
        if (!strcmp(use->unresolved, "implements")) {
          printf("Found implementation phrase\n");
          //Next, fine the interface this is talking about
          if (UnresolvedSymExpr *interface_name = toUnresolvedSymExpr(ce->argList.tail)) {
            forv_Vec (InterfaceSymbol, is, gInterfaceSymbols) {
              printf("Checking against: %s\n", is->cname);
              if (!strcmp(is->cname, interface_name->unresolved))
                printf("Found interface\n");
            }
          }
          else if (SymExpr *se = toSymExpr(ce->argList.tail)) {
            printf("Found interface: %p\n", se);
            //if (SymExpr *implementing_type = toSymExpr(ce->argList.head)) {
            if (isSymExpr(ce->argList.head)) {
              if (InterfaceSymbol *is = toInterfaceSymbol(se->var)) {
                forv_Vec (FnSymbol, fn, is->functionSignatures) {
                  CallInfo2 info(fn);
                }
              }
            }
            else {
              printf("Implementing type not found\n");
            }
          }
        }
      }
    }
  }
}

void earlyTypeCheck(void) { 
  bool found_early_type_checked = false;
  checkInterfaceImplementations(userModules.v[0]->block);

  forv_Vec(FnSymbol, fn, gFnSymbols) {
    if (fn->hasFlag(FLAG_SEPARATELY_TYPE_CHECKED)) {
      found_early_type_checked = true;
      typeCheckFn(fn);
    }
  }  
  if (found_early_type_checked) {
    //Hackish workaround to stop early when we're early type-checking until we
    //tie into the rest of the passes
    INT_FATAL("SUCCESS");
  }
}