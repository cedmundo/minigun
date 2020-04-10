#pragma once
#include "ast.h"

struct value;
struct bind;
struct scope;
struct function;

enum type {
  TYPE_UNIT,
  TYPE_FUNCTION,
  TYPE_STRING,
  TYPE_NUMBER,
  TYPE_ERROR,
};
struct value {
  union {
    struct function *fun;
    double num;
    char *str;
  };
  enum type type;
};

struct bind {
  struct bind *next;
  char *id;
  struct value value;
};

struct scope {
  struct scope *parent;
  struct bind *binds;
};

typedef struct value (*native_def)(struct scope *);

enum function_type {
  FUN_NATIVE,
  FUN_DEF,
};
struct function {
  native_def nat_ref;
  struct def_expr def_ref;
  enum function_type type;
};

struct value run_lit_expr(struct scope *scope, struct lit_expr *lit_expr);
struct value run_lookup_expr(struct scope *scope,
                             struct lookup_expr *lookup_expr);
struct value run_bin_expr(struct scope *scope, struct bin_expr *bin_expr);
struct value run_call_expr(struct scope *scope, struct call_expr *call_expr);
struct value run_def_expr(struct scope *scope, struct def_expr *def_expr);
struct value run_let_expr(struct scope *scope, struct let_expr *let_expr);
struct value run_if_expr(struct scope *scope, struct if_expr *if_expr);
struct value run_for_expr(struct scope *scope, struct for_expr *for_expr);
struct value run_expr(struct scope *scope, struct expr *expr);

void run_all_def_exprs(struct scope *scope, struct def_exprs *def_exprs);
void run_main(struct scope *scope);

void free_value(struct value *value);

struct scope *scope_fork(struct scope *parent);
void scope_exit(struct scope *scope);

void scope_bind(struct scope *scope, const char *id, struct value value);
struct bind *scope_resolve(struct scope *scope, const char *id);

struct scope *scope_builtins(struct scope *scope);

#define make_error(res, msg)                                                   \
  do {                                                                         \
    fprintf(stderr, (msg));                                                    \
    fprintf(stderr, "\n");                                                     \
    res.type = TYPE_ERROR;                                                     \
    const char *errmsg = (msg);                                                \
    const size_t msgsize = strlen(errmsg);                                     \
    res.str = malloc(msgsize + 1);                                             \
    memset(res.str, 0L, msgsize);                                              \
    memcpy(res.str, errmsg, msgsize);                                          \
  } while (0);

#define make_errorf(res, msg, ...)                                             \
  do {                                                                         \
    fprintf(stderr, (msg), __VA_ARGS__);                                       \
    fprintf(stderr, "\n");                                                     \
    res.type = TYPE_ERROR;                                                     \
    const char *errmsg = (msg);                                                \
    const size_t msgsize = strlen(errmsg);                                     \
    res.str = malloc(msgsize + 1);                                             \
    memset(res.str, 0L, msgsize);                                              \
    sprintf(res.str, msg, __VA_ARGS__);                                        \
  } while (0);
