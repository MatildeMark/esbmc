#include <unistd.h>

#include <sstream>

#include "smtlib_conv.h"
#include "y.tab.hpp"

// Dec of external lexer input stream
extern "C" FILE *smtlibin;
int smtlibparse(int startval);
extern int smtlib_send_start_code;
extern sexpr *smtlib_output;

smtlib_convt::smtlib_convt(bool int_encoding, const namespacet &_ns,
                           bool is_cpp, const optionst &_opts)
  : smt_convt(false, int_encoding, _ns, is_cpp, false), options(_opts)
{
  // Setup: open a pipe to the smtlib solver. Because C++ is terrible,
  // there's no standard way of opening a stream from an fd, we can try
  // a nonportable way in the future if fwrite becomes unenjoyable.

  int inpipe[2], outpipe[2];
  std::string cmd;

  cmd = options.get_option("smtlib-solver-prog");
  if (cmd == "") {
    std::cerr << "Must specify an smtlib solver program in smtlib mode"
              << std::endl;
    abort();
  }

  if (pipe(inpipe) != 0) {
    std::cerr << "Couldn't open a pipe for smtlib solver" << std::endl;
    abort();
  }

  if (pipe(outpipe) != 0) {
    std::cerr << "Couldn't open a pipe for smtlib solver" << std::endl;
    abort();
  }

  solver_proc_pid = fork();
  if (solver_proc_pid == 0) {
    close(outpipe[1]);
    close(inpipe[0]);
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    dup2(outpipe[0], STDIN_FILENO);
    dup2(inpipe[1], STDOUT_FILENO);
    close(outpipe[0]);
    close(inpipe[1]);

    // Voila
    execlp(cmd.c_str(), cmd.c_str(), NULL);
    std::cerr << "Exec of smtlib solver failed" << std::endl;
    abort();
  } else {
    close(outpipe[0]);
    close(inpipe[1]);
    out_stream = fdopen(outpipe[1], "w");
    in_stream = fdopen(inpipe[0], "r");
  }

  // Execution continues as the parent ESBMC process. Child dying will
  // trigger SIGPIPE or an EOF eventually, which we'll be able to detect
  // and crash upon.

  // Point lexer input at output stream
  smtlibin = in_stream;

  // Fetch solver name and version.
  fprintf(out_stream, "(get-info :name)\n");
  fflush(out_stream);
  smtlib_send_start_code = 1;
  smtlibparse(TOK_START_INFO);

  // As a result we should have a single entry in a list of sexprs.
  struct sexpr *sexpr = smtlib_output;
  assert(sexpr->sexpr_list.size() == 1 &&
         "More than one sexpr response to get-info name");
  struct sexpr &s = sexpr->sexpr_list.front();

  // Should have a keyword followed by a string?
  assert(s.token == 0 && s.sexpr_list.size() == 2 && "Bad solver name format");
  struct sexpr &keyword = s.sexpr_list.front();
  struct sexpr &value = s.sexpr_list.back();
  assert(keyword.token == TOK_KEYWORD && keyword.data == ":name" &&
         "Bad get-info :name response from solver");
  assert(value.token == TOK_STRINGLIT && "Non-string solver name response");
  solver_name = value.data;
  delete smtlib_output;

  // Duplicate / boilerplate;
  fprintf(out_stream, "(get-info :version)\n");
  fflush(out_stream);
  smtlib_send_start_code = 1;
  smtlibparse(TOK_START_INFO);

  sexpr = smtlib_output;
  assert(sexpr->sexpr_list.size() == 1 &&
         "More than one sexpr response to get-info version");
  struct sexpr &v = sexpr->sexpr_list.front();

  assert(v.token == 0 && v.sexpr_list.size() == 2 && "Bad solver version fmt");
  struct sexpr &kw = v.sexpr_list.front();
  struct sexpr &val = v.sexpr_list.back();
  assert(kw.token == TOK_KEYWORD && kw.data == ":version" &&
         "Bad get-info :version response from solver");
  assert(val.token == TOK_STRINGLIT && "Non-string solver version response");
  solver_version = val.data;
  delete smtlib_output;
}

smtlib_convt::~smtlib_convt()
{
}

prop_convt::resultt
smtlib_convt::dec_solve()
{
  abort();
}

expr2tc
smtlib_convt::get(const expr2tc &expr __attribute__((unused)))
{
  abort();
}

tvt
smtlib_convt::l_get(literalt a __attribute__((unused)))
{
  abort();
}

const std::string
smtlib_convt::solver_text()
{
  abort();
}

void
smtlib_convt::assert_lit(const literalt &l __attribute__((unused)))
{
  abort();
}

smt_ast *
smtlib_convt::mk_func_app(const smt_sort *s __attribute__((unused)), smt_func_kind k __attribute__((unused)),
                          const smt_ast **args __attribute__((unused)), unsigned int numargs __attribute__((unused)))
{
  abort();
}

smt_sort *
smtlib_convt::mk_sort(const smt_sort_kind k __attribute__((unused)), ...)
{
  va_list ap;
  smtlib_smt_sort *s = NULL, *dom, *range;
  unsigned long uint;
  int thebool;

  va_start(ap, k);
  switch (k) {
  case SMT_SORT_INT:
    thebool = va_arg(ap, int);
    s = new smtlib_smt_sort(k, thebool);
    break;
  case SMT_SORT_REAL:
    s = new smtlib_smt_sort(k);
    break;
  case SMT_SORT_BV:
    uint = va_arg(ap, unsigned long);
    thebool = va_arg(ap, int);
    s = new smtlib_smt_sort(k, uint, thebool);
    break;
  case SMT_SORT_ARRAY:
    dom = va_arg(ap, smtlib_smt_sort *); // Consider constness?
    range = va_arg(ap, smtlib_smt_sort *);
    s = new smtlib_smt_sort(k, dom, range);
    break;
  case SMT_SORT_BOOL:
    s = new smtlib_smt_sort(k);
    break;
  default:
    assert(0);
  }

  return s;
}

literalt
smtlib_convt::mk_lit(const smt_ast *s)
{
  const smt_ast *args[2];
  smt_sort *sort = mk_sort(SMT_SORT_BOOL);
  std::stringstream ss;
  literalt l = new_variable();
  ss << "l" << l.get();
  args[0] = mk_smt_symbol(ss.str(), sort);;
  args[1] = s;
  smt_ast *eq = mk_func_app(sort, SMT_FUNC_EQ, args, 2);
  assertion_list.push_back(static_cast<const smtlib_smt_ast *>(eq));
  return l;
}

smt_ast *
smtlib_convt::mk_smt_int(const mp_integer &theint, bool sign)
{
  smt_sort *s = mk_sort(SMT_SORT_INT, sign);
  smtlib_smt_ast *a = new smtlib_smt_ast(s, SMT_FUNC_INT);
  a->intval = theint;
  return a;
}

smt_ast *
smtlib_convt::mk_smt_real(const mp_integer &thereal)
{
  smt_sort *s = mk_sort(SMT_SORT_REAL);
  smtlib_smt_ast *a = new smtlib_smt_ast(s, SMT_FUNC_REAL);
  a->intval = thereal;
  return a;
}

smt_ast *
smtlib_convt::mk_smt_bvint(const mp_integer &theint, bool sign, unsigned int w)
{
  smt_sort *s = mk_sort(SMT_SORT_BV, w, sign);
  smtlib_smt_ast *a = new smtlib_smt_ast(s, SMT_FUNC_BVINT);
  a->intval = theint;
  return a;
}

smt_ast *
smtlib_convt::mk_smt_bool(bool val)
{
  smtlib_smt_ast *a = new smtlib_smt_ast(mk_sort(SMT_SORT_BOOL), SMT_FUNC_BOOL);
  a->boolval = val;
  return a;
}

smt_ast *
smtlib_convt::mk_smt_symbol(const std::string &name, const smt_sort *s)
{
  smtlib_smt_ast *a = new smtlib_smt_ast(s, SMT_FUNC_SYMBOL);
  a->symname = name;
  symbol_table[name] = s;
  return a;
}

smt_sort *
smtlib_convt::mk_struct_sort(const type2tc &type __attribute__((unused)))
{
  std::cerr << "Attempted to make struct type in smtlib conversion" <<std::endl;
  abort();
}

smt_sort *
smtlib_convt::mk_union_sort(const type2tc &type __attribute__((unused)))
{
  std::cerr << "Attempted to make union type in smtlib conversion" << std::endl;
  abort();
}

smt_ast *
smtlib_convt::mk_extract(const smt_ast *a, unsigned int high, unsigned int low,
                         const smt_sort *s)
{
  smtlib_smt_ast *n = new smtlib_smt_ast(s, SMT_FUNC_EXTRACT);
  n->extract_high = high;
  n->extract_low = low;
  n->args[0] = a;
  return n;
}

int
smtliberror(int startsym __attribute__((unused)), const std::string &error)
{
  std::cerr << "SMTLIB response parsing error: \"" << error << "\""
            << std::endl;
  abort();
}
