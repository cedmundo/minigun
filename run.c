#define _GNU_SOURCE
#include "run.h"
#include "binops.h"
#include "scope.h"
#include <assert.h>
#include <iconv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct value run_lit_expr(struct scope *scope, struct lit_expr *lit_expr) {
  assert(scope != NULL);
  assert(lit_expr != NULL);

  struct value res;
  if (lit_expr->type == LIT_STRING) {
    res.type = TYPE_STRING;
    size_t quoted_size = strlen(lit_expr->raw_value);
    res.str = strndup(lit_expr->raw_value + 1, quoted_size - 2);
    scope_bind(scope, NULL, res, BIND_OWNER);
  } else {
    if (strstr(lit_expr->raw_value, ".") != NULL) {
      res.type = TYPE_F64;
      res.f64 = strtod(lit_expr->raw_value, NULL);
    } else if (strstr(lit_expr->raw_value, "-") != NULL) {
      res.type = TYPE_I64;
      res.i64 = strtol(lit_expr->raw_value, NULL, 10);
    } else {
      res.type = TYPE_U64;
      res.u64 = strtoul(lit_expr->raw_value, NULL, 10);
    }
  }
  return res;
}

struct value run_lookup_expr(struct scope *scope,
                             struct lookup_expr *lookup_expr) {
  assert(scope != NULL);
  assert(lookup_expr != NULL);
  struct value res;
  struct bind *value_bind = scope_resolve(scope, lookup_expr->id);
  if (value_bind == NULL) {
    make_errorf(res, "%s is not defined", lookup_expr->id);
    scope_bind(scope, NULL, res, BIND_OWNER);
    return res;
  }

  res = copy_value(value_bind->value);
  scope_bind(scope, NULL, res, BIND_OWNER);
  return res;
}

struct value run_bin_expr(struct scope *scope, struct bin_expr *bin_expr) {
  assert(scope != NULL);
  assert(bin_expr != NULL);
  struct value left_value = run_expr(scope, bin_expr->left);
  struct value right_value = run_expr(scope, bin_expr->right);

  struct value res = handle_bin_op(left_value, right_value, bin_expr->op);
  scope_bind(scope, NULL, res, BIND_OWNER);
  return res;
}

struct value run_unit_expr(struct scope *scope, struct unit_expr *unit_expr) {
  assert(scope != NULL);
  assert(unit_expr != NULL);
  struct value res;
  struct value right = run_expr(scope, unit_expr->right);
  scope_bind(scope, NULL, right, BIND_OWNER);
  if (unit_expr->op == OP_NEG) {
    switch (right.type) {
    case TYPE_U64:
    case TYPE_I64:
      res.type = TYPE_I64;
      res.i64 = -right.i64;
      break;
    case TYPE_F64:
      res.type = TYPE_F64;
      res.f64 = -right.f64;
      break;
    default:
      make_error(res, "unsupported operation for type");
      scope_bind(scope, NULL, res, BIND_OWNER);
      break;
    }
  } else if (unit_expr->op == OP_NOT) {
    switch (right.type) {
    case TYPE_U64:
    case TYPE_I64:
      res.type = TYPE_I64;
      res.i64 = !right.i64;
      break;
    case TYPE_F64:
      res.type = TYPE_F64;
      res.f64 = !right.f64;
      break;
    default:
      make_error(res, "unsupported operation for type");
      scope_bind(scope, NULL, res, BIND_OWNER);
      break;
    }
  } else {
    make_error(res, "unrecognized unitary operation");
    scope_bind(scope, NULL, res, BIND_OWNER);
  }

  return res;
}

struct value run_call_expr(struct scope *scope, struct call_expr *call_expr) {
  assert(scope != NULL);
  assert(call_expr != NULL);

  struct value res;
  // resolve function
  struct bind *fun_bind = scope_resolve(scope, call_expr->callee);
  if (fun_bind == NULL) {
    make_errorf(res, "undefined function '%s'", call_expr->callee);
    scope_bind(scope, NULL, res, BIND_OWNER);
    return res;
  }
  struct value fun_value = fun_bind->value;
  if (fun_value.type != TYPE_FUNCTION) {
    make_errorf(res, "%s is not a function", call_expr->callee);
    scope_bind(scope, NULL, res, BIND_OWNER);
    return res;
  }

  struct scope *forked = scope_fork(scope->global);
  struct function *fun = fun_value.fun;
  if (fun->type == FUN_DEF) {
    struct def_expr *def_expr = fun->def_ref;
    struct def_params *recv_param = def_expr->params;
    struct call_args *send_param = call_expr->args;
    while (send_param != NULL) {
      if (recv_param == NULL) {
        scope_leave(forked);
        free(forked);
        make_errorf(res, "%s expects more arguments", call_expr->callee);
        scope_bind(scope, NULL, res, BIND_OWNER);
        return res;
      }

      struct value local_arg = run_expr(scope, send_param->expr);
      scope_bind(scope, NULL, local_arg, BIND_BORROW);

      struct value copied_arg = copy_value(local_arg);
      scope_bind(forked, recv_param->id, copied_arg, BIND_OWNER);
      send_param = send_param->next;
      recv_param = recv_param->next;
    }

    struct value res_within_forked = run_expr(forked, def_expr->body);
    scope_bind(forked, NULL, res_within_forked, BIND_BORROW);

    res = copy_value(res_within_forked);
    scope_bind(scope, NULL, res, BIND_OWNER);

    scope_leave(forked);
    free(forked);
    return res;
  } else {
    make_errorf(res, "'%s' is a native function and is not supported yet",
                call_expr->callee);
    scope_bind(scope, NULL, res, BIND_OWNER);
    return res;
  }
}

struct value run_let_expr(struct scope *scope, struct let_expr *let_expr) {
  assert(scope != NULL);
  assert(let_expr != NULL);
  struct scope *forked = scope_fork(scope);

  struct let_assigns *assign = let_expr->assigns;
  while (assign != NULL) {
    struct value value = run_expr(scope, assign->expr);
    scope_bind(forked, assign->id, value, BIND_BORROW);
    assign = assign->next;
  }

  struct value res_within_forked = run_expr(forked, let_expr->in_expr);
  scope_bind(forked, NULL, res_within_forked, BIND_BORROW);

  struct value res = copy_value(res_within_forked);
  scope_bind(scope, NULL, res, BIND_OWNER);

  scope_leave(forked);
  free(forked);
  return res;
}

struct value run_def_expr(struct scope *scope, struct def_expr *def_expr) {
  assert(scope != NULL);
  assert(def_expr != NULL);
  struct value fun_value;
  struct function *fun = malloc(sizeof(struct function));
  fun->type = FUN_DEF;
  fun->def_ref = def_expr;
  fun_value.type = TYPE_FUNCTION;
  fun_value.fun = fun;
  scope_bind(scope, def_expr->id, fun_value, BIND_OWNER);
  return fun_value;
}

struct value run_if_expr(struct scope *scope, struct if_expr *if_expr) {
  assert(scope != NULL);
  assert(if_expr != NULL);

  struct value res;
  struct value cond_value = run_expr(scope, if_expr->cond_expr);
  scope_bind(scope, NULL, cond_value, BIND_BORROW);

  if (cond_value.type == TYPE_UNIT) {
    make_error(res, "cannot evaluate condition for unit type");
    scope_bind(scope, NULL, res, BIND_OWNER);
    return res;
  }

  if (cond_value.u64) {
    res = run_expr(scope, if_expr->then_expr);
  } else {
    res = run_expr(scope, if_expr->else_expr);
  }

  scope_bind(scope, NULL, res, BIND_BORROW);
  return res;
}

struct value run_expr(struct scope *scope, struct expr *expr) {
  assert(scope != NULL);
  assert(expr != NULL);

  struct value res;
  switch (expr->type) {
  case EXPR_LIT:
    res = run_lit_expr(scope, expr->lit_expr);
    break;
  case EXPR_LOOKUP:
    res = run_lookup_expr(scope, expr->lookup_expr);
    break;
  case EXPR_BIN:
    res = run_bin_expr(scope, expr->bin_expr);
    break;
  case EXPR_UNIT:
    res = run_unit_expr(scope, expr->unit_expr);
    break;
  case EXPR_CALL:
    res = run_call_expr(scope, expr->call_expr);
    break;
  case EXPR_LET:
    res = run_let_expr(scope, expr->let_expr);
    break;
  case EXPR_DEF:
    res = run_def_expr(scope, expr->def_expr);
    break;
  case EXPR_IF:
    res = run_if_expr(scope, expr->if_expr);
    break;
  default:
    make_errorf(res, "unknown expression type: %d", expr->type);
    scope_bind(scope, NULL, res, BIND_OWNER);
  }

  return res;
}

void run_main(struct scope *scope) {
  assert(scope != NULL);
  struct call_expr call_main = {.callee = "main"};
  struct value final_value = run_call_expr(scope, &call_main);
  print_value(final_value);
  scope_bind(scope, NULL, final_value, BIND_BORROW);
}

struct list *copy_list(struct list *list) {
  struct list *head = NULL;
  struct list *tail = NULL;
  struct list *cur = list, *tmp = NULL;

  while (cur != NULL) {
    tmp = malloc(sizeof(struct list));
    tmp->next = NULL;
    tmp->value = cur->value;
    cur = cur->next;

    if (head == NULL) {
      head = tmp;
    }

    if (tail != NULL) {
      tail->next = tmp;
    }

    tail = tmp;
  }

  return head;
}

struct value copy_value(struct value value) {
  struct value copy = {.type = value.type};
  switch (value.type) {
  case TYPE_UNIT:
    break;
  case TYPE_FUNCTION:
    copy.fun = malloc(sizeof(struct function));
    memcpy(copy.fun, value.fun, sizeof(struct function));
    break;
  case TYPE_ERROR:
  case TYPE_STRING:
    copy.str = strdup(value.str);
    break;
  case TYPE_LIST:
    copy.list = copy_list(value.list);
    break;
  case TYPE_U64:
  case TYPE_I64:
  case TYPE_F64:
    memcpy(&copy, &value, sizeof(struct value));
    break;
  }
  return copy;
}

void print_value(struct value value) {
  switch (value.type) {
  case TYPE_UNIT:
    printf("unit");
    break;
  case TYPE_FUNCTION:
    printf("function");
    break;
  case TYPE_U64:
    printf("u64(%lu)", value.u64);
    break;
  case TYPE_I64:
    printf("i64(%lu)", value.i64);
    break;
  case TYPE_F64:
    printf("f64(%lf)", value.f64);
    break;
  case TYPE_STRING:
    printf("string('%s')", value.str);
    break;
  case TYPE_LIST:
    printf("list[");
    struct list *item = value.list;
    while (item != NULL) {
      print_value(item->value);
      item = item->next;
      if (item != NULL) {
        printf(",");
      }
    }
    printf("]");
    break;
  case TYPE_ERROR:
    printf("error('%s')", value.str);
    break;
  }
}

void free_value(struct value *value) {
  struct list *item = NULL, *tmp = NULL;
  switch (value->type) {
  case TYPE_FUNCTION:
    if (value->fun != NULL) {
      free(value->fun);
    }
    break;
  case TYPE_LIST:
    item = value->list;
    while (item != NULL) {
      tmp = item->next;
      free(item);
      item = tmp;
    }
    break;
  case TYPE_STRING:
  case TYPE_ERROR:
    if (value->str != NULL) {
      free(value->str);
    }
    break;
  case TYPE_UNIT:
  case TYPE_U64:
  case TYPE_I64:
  case TYPE_F64:
    break;
  }
}

void run_all_def_exprs(struct scope *scope, struct def_exprs *def_exprs) {
  assert(scope != NULL);
  scope->def_exprs = def_exprs;
  while (def_exprs != NULL) {
    run_def_expr(scope, def_exprs->def_expr);
    def_exprs = def_exprs->next;
  }
}
