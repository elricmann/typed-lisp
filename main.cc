// Copyright (c) 2025 Elric Neumann. All rights reserved. MIT license.
#include <llvm/ADT/APFloat.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/Bitcode/BitcodeWriter.h> /*!*/
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/ValueSymbolTable.h>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/Support/ErrorHandling.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Host.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stack>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace typed_lisp {

#define TOKEN_PROGRAM "program"
#define TOKEN_TRUE "true"
#define TOKEN_FALSE "false"
#define TOKEN_LET "let"
#define TOKEN_SET "set"
#define TOKEN_IF "if"
#define TOKEN_DEF "def"
#define TOKEN_COLON ":"
#define TOKEN_QUOTE '"'
#define TOKEN_LPAREN '('
#define TOKEN_RPAREN ')'
#define TOKEN_ADD "+"
#define TOKEN_SUB "-"
#define TOKEN_MUL "*"
#define TOKEN_DIV "/"
#define TOKEN_EQ "="
#define TOKEN_NEQ "!="
#define TOKEN_LT "<"
#define TOKEN_GT ">"
#define TOKEN_LEQ "<="
#define TOKEN_GEQ ">="

#define TYPE_INT "int"
#define TYPE_BOOL "bool"
#define TYPE_FLOAT "float"
#define TYPE_DOUBLE "double"
#define TYPE_CHAR "char"
#define TYPE_STRING "string"
#define TYPE_POLYMORPHIC_SPECIFIER '\''

class node {
 public:
  virtual ~node() = default;
  virtual void accept(class node_visitor* visitor) = 0;
};

class atom : public node {
 public:
  std::string value;

  explicit atom(std::string val) : value(std::move(val)) {}

  void accept(node_visitor* visitor) override;
};

class list : public node, public std::enable_shared_from_this<list> {
 public:
  std::vector<std::shared_ptr<node>> children;

  void accept(node_visitor* visitor) override;
};

class node_visitor {
 public:
  virtual void visit(atom* node) = 0;
  virtual void visit(list* node) = 0;
  virtual ~node_visitor() = default;
};

void atom::accept(node_visitor* visitor) { visitor->visit(this); }

void list::accept(node_visitor* visitor) {
  visitor->visit(this);

  for (auto& child : children) {
    child->accept(visitor);
  }
}

class lisp_parser {
 private:
  std::string input;
  size_t current_pos = 0;
  size_t current_line = 1;
  size_t current_column = 1;

  std::string get_line_at(size_t line_number) const {
    std::istringstream stream(input);
    std::string line;

    for (size_t i = 1; i <= line_number; ++i) {
      if (!std::getline(stream, line)) {
        return "";
      }
    }

    return line;
  }

  void skip_whitespace() {
    while (current_pos < input.length()) {
      if (std::isspace(input[current_pos])) {
        current_pos++;                         // regular whitespace
      } else if (input[current_pos] == ';') {  // comments
        while (current_pos < input.length() && input[current_pos] != '\n') {
          current_pos++;
        }
      } else {
        break;
      }
    }
  }

  std::shared_ptr<node> parse_expression() {
    skip_whitespace();

    if (current_pos >= input.length()) {
      throw std::runtime_error("unexpected end of input");
    }

    if (input[current_pos] == TOKEN_LPAREN) {
      return parse_list();
    }

    return parse_atom();
  }

  std::shared_ptr<list> parse_list() {
    if (input[current_pos] != TOKEN_LPAREN) {
      throw std::runtime_error("expected opening parenthesis");
    }

    current_pos++;

    auto lst = std::make_shared<list>();

    while (current_pos < input.length() && input[current_pos] != TOKEN_RPAREN) {
      lst->children.push_back(parse_expression());
      skip_whitespace();
    }

    if (current_pos >= input.length()) {
      throw std::runtime_error("unclosed list");
    }

    current_pos++;

    return lst;
  }

  std::shared_ptr<atom> parse_atom() {
    size_t start = current_pos;

    while (current_pos < input.length() && !std::isspace(input[current_pos]) &&
           input[current_pos] != TOKEN_LPAREN &&
           input[current_pos] != TOKEN_RPAREN) {
      current_pos++;
    }

    std::string value = input.substr(start, current_pos - start);

    return std::make_shared<atom>(value);
  }

 public:
  explicit lisp_parser(std::string input_str) : input(std::move(input_str)) {}

  std::shared_ptr<node> parse() {
    current_pos = 0;

    return parse_expression();
  }

  std::pair<size_t, size_t> get_current_location() const {
    return {current_line, current_column};
  }

  std::string get_context_line(size_t line_number) const {
    return get_line_at(line_number);
  }
};

std::string format_error(const std::string& message, size_t line, size_t column,
                         const std::string& context,
                         const std::string& type_repr,
                         const std::string& hint) {
  std::ostringstream oss;

  const std::string red = "\033[1;31m";
  const std::string green = "\033[1;32m";
  const std::string yellow = "\033[1;33m";
  const std::string blue = "\033[1;34m";
  const std::string purple = "\033[1;35m";
  const std::string reset = "\033[0m";

  oss << red << "error: " << reset << message << "\n";
  oss << purple << "  @ " << reset << "line " << line << ", col " << column
      << "\n";

  oss << blue << "  | " << reset << "\n";
  oss << blue << "  | " << reset << context << "\n";
  oss << blue << "  | " << reset << std::setw(column) << "^" << "\n";
  oss << yellow << "  hint: " << reset << hint;

  // @todo: optional detailed hints that can be enabled on error
  // if (type_repr.size() > 0) {
  //   oss << "\n\n        " << "??" << "\n";
  //   oss << purple << "     —————————" << reset << " ... " << type_repr
  //       << " ∈ Γ without implication"
  //       << "\n";  // @todo: there needs to be flags for error kinds
  //   oss << purple << "      Γ ⊢ " << reset << type_repr << "\n";
  //   oss << "\n  constraint is unsatisfied unless deducing from opaque "
  //          "context.";
  // }

  return oss.str();
}

class tokens_print_visitor : public node_visitor {
 public:
  void visit(atom* node) override { std::cout << node->value << " "; }

  void visit(list* node) override {
    std::cout << "\nsize: " << node->children.size() << std::endl;
    // std::cout << "( ";
  }
};

struct type;
using type_ptr = std::shared_ptr<type>;

struct type_var {
  static int next_id;
  int id;
  type_var() : id(next_id++) {}
};

int type_var::next_id = 0;

struct type {
  virtual ~type() = default;
  virtual std::string to_string() const = 0;
  virtual type_ptr substitute(
      const std::unordered_map<int, type_ptr>& subst) const = 0;
  virtual std::vector<int> free_vars() const = 0;
};

struct atomic_type : type {
  std::string name;

  explicit atomic_type(std::string n) : name(std::move(n)) {}

  std::string to_string() const override { return name; }

  type_ptr substitute(
      const std::unordered_map<int, type_ptr>& subst) const override {
    return std::make_shared<atomic_type>(*this);
  }

  std::vector<int> free_vars() const override { return {}; }
};

struct var_type : type {
  int id;
  explicit var_type(int i) : id(i) {}

  std::string to_string() const override {
    std::stringstream ss;
    ss << "t" << id;
    return ss.str();
  }

  type_ptr substitute(
      const std::unordered_map<int, type_ptr>& subst) const override {
    auto it = subst.find(id);
    return it != subst.end() ? it->second : std::make_shared<var_type>(*this);
  }

  std::vector<int> free_vars() const override { return {id}; }
};

struct func_type : type {
  type_ptr arg_type;
  type_ptr ret_type;

  func_type(type_ptr arg, type_ptr ret)
      : arg_type(std::move(arg)), ret_type(std::move(ret)) {}

  std::string to_string() const override {
    return "(" + arg_type->to_string() + " -> " + ret_type->to_string() + ")";
  }

  type_ptr substitute(
      const std::unordered_map<int, type_ptr>& subst) const override {
    return std::make_shared<func_type>(arg_type->substitute(subst),
                                       ret_type->substitute(subst));
  }

  std::vector<int> free_vars() const override {
    auto arg_vars = arg_type->free_vars();
    auto ret_vars = ret_type->free_vars();
    arg_vars.insert(arg_vars.end(), ret_vars.begin(), ret_vars.end());

    return arg_vars;
  }
};

class type_env {
  std::unordered_map<std::string, type_ptr> env;

 public:
  void insert(const std::string& name, type_ptr t) { env[name] = std::move(t); }

  type_ptr lookup(const std::string& name) const {
    auto it = env.find(name);

    if (it == env.end()) throw std::runtime_error("unbound variable: " + name);

    return it->second;
  }
};

class type_system {
  std::unordered_map<int, type_ptr> substitutions;

  bool occurs_check(int var_id, const type_ptr& t) {
    auto vars = t->free_vars();

    return std::find(vars.begin(), vars.end(), var_id) != vars.end();
  }

  type_ptr apply_substitution(const type_ptr& t) {
    return t->substitute(substitutions);
  }

 public:
  void unify(type_ptr t1, type_ptr t2) {
    t1 = apply_substitution(t1);
    t2 = apply_substitution(t2);

    if (auto v1 = std::dynamic_pointer_cast<var_type>(t1)) {
      if (t1 != t2) {
        if (occurs_check(v1->id, t2)) {
          throw std::runtime_error("recursive unification");
        }

        substitutions[v1->id] = t2;
      }

      return;
    }

    if (auto v2 = std::dynamic_pointer_cast<var_type>(t2)) {
      unify(t2, t1);
      return;
    }

    auto f1 = std::dynamic_pointer_cast<func_type>(t1);
    auto f2 = std::dynamic_pointer_cast<func_type>(t2);

    if (f1 && f2) {
      unify(f1->arg_type, f2->arg_type);
      unify(f1->ret_type, f2->ret_type);
      return;
    }

    auto a1 = std::dynamic_pointer_cast<atomic_type>(t1);
    auto a2 = std::dynamic_pointer_cast<atomic_type>(t2);

    if (a1 && a2 && a1->name == a2->name) {
      return;
    }

    throw std::runtime_error("type mismatch, expected " + t1->to_string() +
                             " but found " + t2->to_string());
  }

  type_ptr fresh_var() { return std::make_shared<var_type>(type_var().id); }

  type_ptr get_type(const std::string& name) {
    return std::make_shared<atomic_type>(name);
  }

  type_ptr make_function_type(type_ptr arg, type_ptr ret) {
    return std::make_shared<func_type>(std::move(arg), std::move(ret));
  }

  type_ptr get_final_type(const type_ptr& t) { return apply_substitution(t); }
};

class scope : public std::enable_shared_from_this<scope> {
  // clang-format off
  std::shared_ptr<scope>                            parent;
  std::vector<std::shared_ptr<scope>>               children;
  type_env                                          env;
  type_system                                       types;
  std::unordered_map<std::string, std::vector<int>> polymorphic_vars;
  // clang-format on

 public:
  explicit scope(std::shared_ptr<scope> p = nullptr) : parent(p) {}

  void add_child(std::shared_ptr<scope> child) { children.push_back(child); }

  std::shared_ptr<scope> create_child() {
    auto child = std::make_shared<scope>(shared_from_this());
    add_child(child);
    return child;
  }

  type_ptr lookup_type(const std::string& name) {
    try {
      auto t = env.lookup(name);

      if (auto poly_vars = get_polymorphic_vars(name)) {
        return instantiate_polymorphic_type(t, *poly_vars);
      }

      return t;
    } catch (const std::runtime_error& e) {
      if (parent) return parent->lookup_type(name);
      std::cout << "=== lookup issue here===\n";
      throw std::runtime_error(e.what());  // ??
    }
  }

  void define_type(const std::string& name, type_ptr t,
                   const std::vector<int>& poly_vars = {}) {
    env.insert(name, t);

    if (!poly_vars.empty()) {
      polymorphic_vars[name] = poly_vars;
    }
  }

  std::optional<std::vector<int>> get_polymorphic_vars(
      const std::string& name) {
    auto it = polymorphic_vars.find(name);

    if (it != polymorphic_vars.end()) {
      return it->second;
    }

    return std::nullopt;
  }

  type_ptr instantiate_polymorphic_type(type_ptr t,
                                        const std::vector<int>& vars) {
    std::unordered_map<int, type_ptr> subst;

    for (int var : vars) {
      subst[var] = types.fresh_var();
    }

    return t->substitute(subst);
  }

  type_system& get_type_system() { return types; }
  std::shared_ptr<scope> get_parent() { return parent; }
};

class type_visitor : public node_visitor,
                     public std::enable_shared_from_this<type_visitor> {
 public:
  std::shared_ptr<scope> global_scope;
  std::shared_ptr<scope> current_scope;

  struct var_binding {
    std::string name;
    type_ptr type;
    std::shared_ptr<node> value;
    std::vector<int> polymorphic_vars;
  };

  // clang-format off

  bool entered_fn_block = false;
  std::unordered_map<std::string, var_binding> bindings;
  std::vector<std::string>                     errors;
  type_ptr                                     current_type;
  std::vector<std::shared_ptr<node>>           call_stack;

  // clang-format on

  type_ptr infer_literal(const std::string& value) {
    if (value == TOKEN_PROGRAM || value == TOKEN_FALSE)
      return current_scope->get_type_system().get_type(TYPE_BOOL);

    try {
      std::stoi(value);
      return current_scope->get_type_system().get_type(TYPE_INT);
    } catch (...) {
    }

    if (value.front() == TOKEN_QUOTE && value.back() == TOKEN_QUOTE)
      return current_scope->get_type_system().get_type(TYPE_STRING);

    if (value.front() == TYPE_POLYMORPHIC_SPECIFIER) {
      auto var = current_scope->get_type_system().fresh_var();
      return var;
    }

    return current_scope->lookup_type(value);
  }

  type_ptr infer_binary_op(const std::string& op, type_ptr lhs, type_ptr rhs) {
    auto& ts = current_scope->get_type_system();

    try {
      ts.unify(lhs, rhs);

      if (op == TOKEN_ADD || op == TOKEN_SUB || op == TOKEN_MUL ||
          op == TOKEN_DIV) {
        auto int_t = ts.get_type(TYPE_INT);
        ts.unify(lhs, int_t);
        return int_t;
      }

      if (op == TOKEN_EQ || op == TOKEN_NEQ || op == TOKEN_LT ||
          op == TOKEN_GT) {
        return ts.get_type(TYPE_BOOL);
      }

      throw std::runtime_error("unknown operator: " + op);
    } catch (const std::runtime_error& e) {
      errors.push_back(e.what());
      return ts.fresh_var();
    }
  }

  void visit_let(list* node) {
    if (node->children.size() != 5) {
      std::shared_ptr<typed_lisp::node> shared_node = node->shared_from_this();
      with_error("malformed let expression", shared_node, nullptr,
                 "expected (let name : type value)");
      return;
    }

    auto name_node = std::dynamic_pointer_cast<atom>(node->children[1]);
    auto colon = std::dynamic_pointer_cast<atom>(node->children[2]);
    auto type_node = std::dynamic_pointer_cast<atom>(node->children[3]);
    auto value_node = node->children[4];

    if (!name_node || !colon || !type_node || colon->value != TOKEN_COLON) {
      std::shared_ptr<typed_lisp::node> shared_node = node->shared_from_this();
      with_error("malformed let expression", shared_node, nullptr,
                 "expected (let name : type value)");
      return;
    }

    std::vector<int> poly_vars;
    type_ptr declared_type;

    if (type_node->value.front() == TYPE_POLYMORPHIC_SPECIFIER) {
      auto var = current_scope->get_type_system().fresh_var();
      poly_vars.push_back(std::dynamic_pointer_cast<var_type>(var)->id);
      declared_type = var;
    } else {
      declared_type =
          current_scope->get_type_system().get_type(type_node->value);
    }

    value_node->accept(this);
    auto value_type = current_type;

    try {
      current_scope->get_type_system().unify(declared_type, value_type);
      current_scope->define_type(name_node->value, declared_type, poly_vars);

      // if (bindings.find(name_node->value) != bindings.end()) {
      //   std::cout << name_node->value << " already defined\n";
      //   with_error("redefinition of variable", node->shared_from_this(),
      //              declared_type, "variable already defined");
      //   return;
      // }

      bindings[name_node->value] = {name_node->value, declared_type, value_node,
                                    poly_vars};
    } catch (const std::runtime_error& e) {
      std::shared_ptr<typed_lisp::node> shared_node = node->shared_from_this();
      with_error("type error in let binding", shared_node, declared_type,
                 std::string(e.what()));
      // errors.push_back("type error in let binding: " +
      // std::string(e.what()));
    }
  }

  void visit_def(list* node) {
    if (node->children.size() < 6) {
      errors.push_back(
          "malformed def expression, expected (def name : return_type (params) "
          "body)");
      return;
    }

    auto name_node = std::dynamic_pointer_cast<atom>(node->children[1]);
    auto colon = std::dynamic_pointer_cast<atom>(node->children[2]);
    auto ret_type_node = std::dynamic_pointer_cast<atom>(node->children[3]);
    auto params = std::dynamic_pointer_cast<list>(node->children[4]);

    if (!name_node || !colon || !ret_type_node || !params ||
        colon->value != TOKEN_COLON) {
      errors.push_back("malformed def expression");
      return;
    }

    auto fn_scope = current_scope->create_child();
    auto prev_scope = current_scope;
    current_scope = fn_scope;

    std::vector<type_ptr> param_types;
    std::vector<int> poly_vars;

    for (size_t i = 0; i < params->children.size(); i += 3) {
      if (i + 2 >= params->children.size()) {
        errors.push_back("malformed parameter list");
        continue;
      }

      auto param_name = std::dynamic_pointer_cast<atom>(params->children[i]);
      auto param_colon =
          std::dynamic_pointer_cast<atom>(params->children[i + 1]);
      auto param_type =
          std::dynamic_pointer_cast<atom>(params->children[i + 2]);

      // std::cout << param_name->value << param_colon->value
      // << param_type->value << "\n";

      if (!param_name || !param_colon || !param_type ||
          param_colon->value != TOKEN_COLON) {
        errors.push_back("malformed parameter");
        continue;
      }

      type_ptr param_t;
      if (param_type->value.front() == TYPE_POLYMORPHIC_SPECIFIER) {
        auto var = current_scope->get_type_system().fresh_var();
        poly_vars.push_back(std::dynamic_pointer_cast<var_type>(var)->id);
        param_t = var;
      } else {
        param_t = current_scope->get_type_system().get_type(param_type->value);
      }

      current_scope->define_type(param_name->value, param_t);
      param_types.push_back(param_t);
    }

    type_ptr ret_t;
    if (ret_type_node->value.front() == TYPE_POLYMORPHIC_SPECIFIER) {
      auto var = current_scope->get_type_system().fresh_var();
      poly_vars.push_back(std::dynamic_pointer_cast<var_type>(var)->id);
      ret_t = var;
    } else {
      ret_t = current_scope->get_type_system().get_type(ret_type_node->value);
    }

    std::cout << "ret_t: " << ret_t->to_string() << "\n";

    auto body = node->children[5];
    entered_fn_block = true;
    body->accept(this);
    auto body_type = current_type;
    entered_fn_block = false;

    try {
      current_scope->get_type_system().unify(ret_t, body_type);
    } catch (const std::runtime_error& e) {
      errors.push_back("return type mismatch: " + std::string(e.what()));
    }

    type_ptr fn_type = ret_t;
    for (auto it = param_types.rbegin(); it != param_types.rend(); ++it) {
      fn_type =
          current_scope->get_type_system().make_function_type(*it, fn_type);
    }

    current_scope = prev_scope;
    current_scope->define_type(name_node->value, fn_type, poly_vars);
  }

  void visit_set(list* node) {
    if (node->children.size() != 3) {
      errors.push_back("malformed set expression, expected (set name value)");
      return;
    }

    auto name_node = std::dynamic_pointer_cast<atom>(node->children[1]);
    auto value_node = node->children[2];

    if (!name_node) {
      errors.push_back("malformed set expression");
      return;
    }

    value_node->accept(this);
    auto value_type = current_type;

    try {
      auto var_type = current_scope->lookup_type(name_node->value);
      current_scope->get_type_system().unify(var_type, value_type);
    } catch (const std::runtime_error& e) {
      // errors.push_back("type error in assignment: " + std::string(e.what()));
      with_error("type error in assignment", name_node, nullptr,
                 std::string(e.what()));
    }
  }

  void visit_if(list* node) {
    if (node->children.size() != 4) {
      errors.push_back("malformed if expression, expected (if cond then else)");
      return;
    }

    node->children[1]->accept(this);
    auto cond_type = current_type;

    try {
      current_scope->get_type_system().unify(
          cond_type, current_scope->get_type_system().get_type(TYPE_BOOL));
    } catch (const std::runtime_error& e) {
      errors.push_back("condition must be boolean: " + std::string(e.what()));
    }

    node->children[2]->accept(this);
    auto then_type = current_type;

    node->children[3]->accept(this);
    auto else_type = current_type;

    try {
      current_scope->get_type_system().unify(then_type, else_type);
      current_type = then_type;
    } catch (const std::runtime_error& e) {
      std::shared_ptr<typed_lisp::node> shared_node = node->shared_from_this();
      with_error("branches have different types", shared_node, nullptr,
                 std::string(e.what()));
      // errors.push_back("branches have different types: " +
      //                  std::string(e.what()));
    }
  }

  void visit_call(list* node) {
    if (node->children.empty()) return;

    auto fn = std::dynamic_pointer_cast<atom>(node->children[0]);
    if (!fn) {
      errors.push_back("expected function name");
      return;
    }

    std::cout << "--> entering call: " << fn->value << "\n";

    std::vector<type_ptr> arg_types;
    for (size_t i = 1; i < node->children.size(); ++i) {
      node->children[i]->accept(this);
      arg_types.push_back(current_type);
    }

    try {
      auto fn_type = current_scope->lookup_type(fn->value);
      auto result_type = current_scope->get_type_system().fresh_var();

      type_ptr expected = result_type;
      for (auto it = arg_types.rbegin(); it != arg_types.rend(); ++it) {
        expected =
            current_scope->get_type_system().make_function_type(*it, expected);
      }

      std::cout << "fn: " << fn->value << "\n";
      std::cout << "expected: " << expected->to_string() << "\n";

      current_scope->get_type_system().unify(fn_type, expected);
      current_type = result_type;
    } catch (const std::runtime_error& e) {
      // errors.push_back("type error in function call: " +
      // std::string(e.what()));
      with_error("type error in call expr", node->shared_from_this(), nullptr,
                 std::string(e.what()));
    }
  }

 public:
  lisp_parser& parser;

  type_visitor(lisp_parser& p) : parser(p) {
    global_scope = std::make_shared<scope>();
    current_scope = global_scope;
  }

  void visit(atom* node) override { current_type = infer_literal(node->value); }

  void visit(list* node) override {
    if (node->children.empty()) return;

    auto fst = std::dynamic_pointer_cast<atom>(node->children[0]);

    if (!fst) {
      errors.push_back("expected atom as first element of list");
      return;
    }

    if (fst->value == TOKEN_LET) {
      visit_let(node);
    } else if (fst->value == TOKEN_DEF) {
      visit_def(node);
    } else if (fst->value == TOKEN_SET) {
      visit_set(node);
    } else if (fst->value == TOKEN_IF) {
      visit_if(node);
    } else {
      visit_call(node);
    }
  }

  // @fix: there is this issue where duplicate logs appear filter based on
  // line-column metadata, errors may need to be unordered_map

  void with_error(const std::string& message, const std::shared_ptr<node>& node,
                  const type_ptr& type = nullptr,
                  const std::string& hint = nullptr) {
    auto [line, column] = parser.get_current_location();
    std::string context = parser.get_context_line(line);
    std::string type_repr = type ? type->to_string() : "";

    errors.push_back(
        format_error(message, line, column, context, type_repr, hint));
  }

  const std::vector<std::string>& get_errors() const { return errors; }
};

void register_builtins(std::shared_ptr<scope> scope) {
  auto& ty = scope->get_type_system();

  // register primitive types
  auto int_t = ty.get_type(TYPE_INT);
  auto bool_t = ty.get_type(TYPE_BOOL);
  auto string_t = ty.get_type(TYPE_STRING);

  auto type_var_a = ty.fresh_var();
  auto type_var_b = ty.fresh_var();

  // we need better heuristics to register ops, i.e. never bind to concrete
  // types. additionally, skip the type specifier token instead of capturing

  scope->define_type(TOKEN_COLON,
                     ty.make_function_type(type_var_a, type_var_b));
  scope->define_type(TOKEN_DEF, ty.make_function_type(type_var_a, type_var_b));
  scope->define_type(TOKEN_LET, ty.make_function_type(type_var_a, type_var_b));
  scope->define_type(TOKEN_SET, ty.make_function_type(type_var_a, type_var_b));
  scope->define_type(TOKEN_IF, ty.make_function_type(type_var_a, type_var_b));
  // @fix: why does this fix the unbound issue?
  scope->define_type(TYPE_INT, ty.make_function_type(type_var_a, type_var_b));
  scope->define_type(TYPE_BOOL, ty.make_function_type(type_var_a, type_var_b));
  scope->define_type("k", ty.make_function_type(type_var_a, type_var_b));

  // we reduce lhs and rhs to a single type, and then unify the two
  // program is a callable type that just composes arg types
  scope->define_type(TOKEN_PROGRAM,
                     ty.make_function_type(type_var_a, type_var_b));

  scope->define_type(
      TOKEN_ADD,
      ty.make_function_type(int_t, ty.make_function_type(int_t, int_t)));
  scope->define_type(
      TOKEN_SUB,
      ty.make_function_type(int_t, ty.make_function_type(int_t, int_t)));
  scope->define_type(
      TOKEN_MUL,
      ty.make_function_type(int_t, ty.make_function_type(int_t, int_t)));
  scope->define_type(
      TOKEN_DIV,
      ty.make_function_type(int_t, ty.make_function_type(int_t, int_t)));

  scope->define_type(
      TOKEN_EQ,
      ty.make_function_type(int_t, ty.make_function_type(int_t, bool_t)));
  scope->define_type(
      TOKEN_GT,
      ty.make_function_type(int_t, ty.make_function_type(int_t, bool_t)));
  scope->define_type(
      TOKEN_LT,
      ty.make_function_type(int_t, ty.make_function_type(int_t, bool_t)));
}

class llvm_codegen;

class codegen_error : public std::runtime_error {
 public:
  explicit codegen_error(const std::string& message)
      : std::runtime_error(message) {}
};

class codegen_scope : public std::enable_shared_from_this<codegen_scope> {
 private:
  std::shared_ptr<codegen_scope> parent;
  std::unordered_map<std::string, llvm::AllocaInst*> value_map;
  std::unordered_map<std::string, llvm::Function*> function_map;

 public:
  explicit codegen_scope(std::shared_ptr<codegen_scope> p = nullptr)
      : parent(p) {}

  void set_value(const std::string& name, llvm::AllocaInst* value) {
    value_map[name] = value;
  }

  void set_function(const std::string& name, llvm::Function* func) {
    function_map[name] = func;
  }

  llvm::AllocaInst* get_value(const std::string& name) const {
    auto it = value_map.find(name);
    if (it != value_map.end()) {
      return it->second;
    }

    if (parent) {
      return parent->get_value(name);
    }

    return nullptr;
  }

  llvm::Function* get_function(const std::string& name) const {
    auto it = function_map.find(name);
    if (it != function_map.end()) {
      return it->second;
    }

    if (parent) {
      return parent->get_function(name);
    }

    return nullptr;
  }

  std::shared_ptr<codegen_scope> create_child() {
    return std::make_shared<codegen_scope>(shared_from_this());
  }
};

struct llvm_type_mapping {
  std::unordered_map<std::string, llvm::Type*> type_map;

  llvm::Type* get_type(llvm::LLVMContext& context, const std::string& name) {
    auto it = type_map.find(name);
    if (it != type_map.end()) {
      return it->second;
    }

    if (name == TYPE_INT) {
      auto result = llvm::Type::getInt32Ty(context);
      type_map[name] = result;
      return result;
    } else if (name == TYPE_BOOL) {
      auto result = llvm::Type::getInt1Ty(context);
      type_map[name] = result;
      return result;
    } else if (name == "void") {
      auto result = llvm::Type::getVoidTy(context);
      type_map[name] = result;
      return result;
    } else if (name == TYPE_STRING) {
      auto result = llvm::Type::getInt8PtrTy(context);
      type_map[name] = result;
      return result;
    } else if (name == TYPE_FLOAT) {
      auto result = llvm::Type::getFloatTy(context);
      type_map[name] = result;
      return result;
    } else if (name == TYPE_DOUBLE) {
      auto result = llvm::Type::getDoubleTy(context);
      type_map[name] = result;
      return result;
    }

    throw codegen_error("unknown type: " + name);
  }
};

struct function_type_info {
  llvm::Type* return_type;
  std::vector<llvm::Type*> param_types;

  llvm::FunctionType* create_function_type() const {
    return llvm::FunctionType::get(return_type, param_types, false);
  }
};

class node_codegen {
 public:
  virtual ~node_codegen() = default;
  virtual llvm::Value* codegen(llvm_codegen& generator) = 0;
};

class atom_codegen : public node_codegen {
 private:
  std::string value;

 public:
  explicit atom_codegen(std::string val) : value(std::move(val)) {}

  llvm::Value* codegen(llvm_codegen& generator) override;
};

class list_codegen : public node_codegen {
 private:
  std::vector<std::shared_ptr<node_codegen>> children;

 public:
  list_codegen(std::vector<std::shared_ptr<node_codegen>> children_nodes)
      : children(std::move(children_nodes)) {}

  llvm::Value* codegen(llvm_codegen& generator) override;
};

class let_codegen : public node_codegen {
 private:
  std::string name;
  std::string type_name;
  std::shared_ptr<node_codegen> value;

 public:
  let_codegen(std::string var_name, std::string type_name,
              std::shared_ptr<node_codegen> value_node)
      : name(std::move(var_name)),
        type_name(std::move(type_name)),
        value(std::move(value_node)) {}

  llvm::Value* codegen(llvm_codegen& generator) override;
};

class set_codegen : public node_codegen {
 private:
  std::string name;
  std::shared_ptr<node_codegen> value;

 public:
  set_codegen(std::string var_name, std::shared_ptr<node_codegen> value_node)
      : name(std::move(var_name)), value(std::move(value_node)) {}

  llvm::Value* codegen(llvm_codegen& generator) override;
};

class if_codegen : public node_codegen {
 private:
  std::shared_ptr<node_codegen> condition;
  std::shared_ptr<node_codegen> then_branch;
  std::shared_ptr<node_codegen> else_branch;

 public:
  if_codegen(std::shared_ptr<node_codegen> cond,
             std::shared_ptr<node_codegen> then_node,
             std::shared_ptr<node_codegen> else_node)
      : condition(std::move(cond)),
        then_branch(std::move(then_node)),
        else_branch(std::move(else_node)) {}

  llvm::Value* codegen(llvm_codegen& generator) override;
};

struct param_info {
  std::string name;
  std::string type_name;
};

class def_codegen : public node_codegen {
 private:
  std::string name;
  std::string return_type_name;
  std::vector<param_info> params;
  std::shared_ptr<node_codegen> body;

 public:
  def_codegen(std::string func_name, std::string ret_type,
              std::vector<param_info> parameters,
              std::shared_ptr<node_codegen> body_node)
      : name(std::move(func_name)),
        return_type_name(std::move(ret_type)),
        params(std::move(parameters)),
        body(std::move(body_node)) {}

  llvm::Value* codegen(llvm_codegen& generator) override;
};

class call_codegen : public node_codegen {
 private:
  std::string name;
  std::vector<std::shared_ptr<node_codegen>> args;

 public:
  call_codegen(std::string func_name,
               std::vector<std::shared_ptr<node_codegen>> arguments)
      : name(std::move(func_name)), args(std::move(arguments)) {}

  llvm::Value* codegen(llvm_codegen& generator) override;
};

class binary_op_codegen : public node_codegen {
 private:
  std::string op;
  std::shared_ptr<node_codegen> lhs;
  std::shared_ptr<node_codegen> rhs;

 public:
  binary_op_codegen(std::string operation, std::shared_ptr<node_codegen> left,
                    std::shared_ptr<node_codegen> right)
      : op(std::move(operation)), lhs(std::move(left)), rhs(std::move(right)) {}

  llvm::Value* codegen(llvm_codegen& generator) override;
};

class llvm_codegen : public std::enable_shared_from_this<llvm_codegen> {
 private:
  std::unique_ptr<llvm::LLVMContext> context;
  std::unique_ptr<llvm::Module> module;
  std::unique_ptr<llvm::IRBuilder<>> builder;

  std::shared_ptr<codegen_scope> global_scope;
  std::shared_ptr<codegen_scope> current_scope;

  llvm_type_mapping type_mapper;

  std::unordered_map<std::string, llvm::Function*> intrinsic_functions;

 public:
  llvm_codegen(const std::string& module_name)
      : context(std::make_unique<llvm::LLVMContext>()),
        module(std::make_unique<llvm::Module>(module_name, *context)),
        builder(std::make_unique<llvm::IRBuilder<>>(*context)) {
    global_scope = std::make_shared<codegen_scope>();
    current_scope = global_scope;

    initialize_intrinsics();
  }

  llvm::LLVMContext& get_context() { return *context; }
  llvm::Module& get_module() { return *module; }
  llvm::IRBuilder<>& get_builder() { return *builder; }

  void set_current_scope(std::shared_ptr<codegen_scope> scope) {
    current_scope = scope;
  }

  std::shared_ptr<codegen_scope> get_current_scope() { return current_scope; }

  std::shared_ptr<codegen_scope> create_new_scope() {
    auto new_scope = current_scope->create_child();
    return new_scope;
  }

  llvm::Type* get_llvm_type(const std::string& type_name) {
    return type_mapper.get_type(*context, type_name);
  }

  llvm::AllocaInst* create_entry_block_alloca(llvm::Function* function,
                                              const std::string& var_name,
                                              llvm::Type* type) {
    llvm::IRBuilder<> temp_builder(&function->getEntryBlock(),
                                   function->getEntryBlock().begin());
    return temp_builder.CreateAlloca(type, nullptr, var_name);
  }

  void initialize_intrinsics();
  llvm::Function* get_intrinsic(const std::string& name);

  function_type_info get_function_type_info(
      const std::string& return_type,
      const std::vector<std::string>& param_types);

  static llvm::AllocaInst* create_entry_block_alloca_for_func(
      llvm::Function* function, const std::string& var_name, llvm::Type* type) {
    llvm::IRBuilder<> temp_builder(&function->getEntryBlock(),
                                   function->getEntryBlock().begin());
    return temp_builder.CreateAlloca(type, nullptr, var_name);
  }

  static llvm::AllocaInst* create_block_alloca(llvm::Function* function,
                                               llvm::BasicBlock* block,
                                               const std::string& var_name,
                                               llvm::Type* type) {
    llvm::IRBuilder<> temp_builder(block, block->begin());
    return temp_builder.CreateAlloca(type, nullptr, var_name);
  }

  void emit_to_file(const std::string& filename);
  void emit_bitcode(const std::string& filename);
  void dump_ir();
};

class codegen_visitor {
 private:
  std::shared_ptr<llvm_codegen> generator;

 public:
  explicit codegen_visitor(std::shared_ptr<llvm_codegen> gen)
      : generator(std::move(gen)) {}

  std::shared_ptr<node_codegen> codegen_node(
      const std::shared_ptr<typed_lisp::node>& node);
};

llvm::Value* atom_codegen::codegen(llvm_codegen& generator) {
  if (value == TOKEN_PROGRAM) {
    return llvm::ConstantInt::get(generator.get_context(),
                                  llvm::APInt(1, 1, false));
  } else if (value == TOKEN_FALSE) {
    return llvm::ConstantInt::get(generator.get_context(),
                                  llvm::APInt(1, 0, false));
  }

  try {
    int int_val = std::stoi(value);
    return llvm::ConstantInt::get(generator.get_context(),
                                  llvm::APInt(32, int_val, true));
  } catch (...) {
  }

  if (value.front() == TOKEN_QUOTE && value.back() == TOKEN_QUOTE) {
    std::string str_val = value.substr(1, value.size() - 2);

    llvm::IRBuilder<>& builder = generator.get_builder();
    llvm::Value* str_constant = builder.CreateGlobalStringPtr(str_val);

    return str_constant;
  }

  llvm::AllocaInst* var = generator.get_current_scope()->get_value(value);
  if (!var) {
    throw codegen_error("undefined variable: " + value);
  }

  return generator.get_builder().CreateLoad(var->getAllocatedType(), var,
                                            value);
}

llvm::Value* list_codegen::codegen(llvm_codegen& generator) {
  if (children.empty()) {
    return llvm::ConstantTokenNone::get(generator.get_context());
  }

  llvm::Value* result = nullptr;
  for (const auto& child : children) {
    result = child->codegen(generator);
  }

  return result;
}

llvm::Value* let_codegen::codegen(llvm_codegen& generator) {
  llvm::Value* val = value->codegen(generator);

  if (!val) {
    throw codegen_error("invalid value in let expression");
  }

  llvm::Type* var_type = generator.get_llvm_type(type_name);

  llvm::Function* func = generator.get_builder().GetInsertBlock()->getParent();
  llvm::AllocaInst* alloca =
      generator.create_entry_block_alloca(func, name, var_type);

  generator.get_builder().CreateStore(val, alloca);

  generator.get_current_scope()->set_value(name, alloca);

  return val;
}

llvm::Value* set_codegen::codegen(llvm_codegen& generator) {
  llvm::Value* val = value->codegen(generator);

  if (!val) {
    throw codegen_error("invalid value in set expression");
  }

  llvm::AllocaInst* var = generator.get_current_scope()->get_value(name);

  if (!var) {
    throw codegen_error("undefined variable: " + name);
  }

  generator.get_builder().CreateStore(val, var);

  return val;
}

llvm::Value* if_codegen::codegen(llvm_codegen& generator) {
  llvm::Value* cond_val = condition->codegen(generator);

  if (!cond_val) {
    throw codegen_error("invalid condition in if expression");
  }

  cond_val = generator.get_builder().CreateICmpNE(
      cond_val,
      llvm::ConstantInt::get(generator.get_context(), llvm::APInt(1, 0, false)),
      "ifcond");

  llvm::Function* func = generator.get_builder().GetInsertBlock()->getParent();

  llvm::BasicBlock* then_bb =
      llvm::BasicBlock::Create(generator.get_context(), "then", func);
  llvm::BasicBlock* else_bb =
      llvm::BasicBlock::Create(generator.get_context(), "else", func);
  llvm::BasicBlock* merge_bb =
      llvm::BasicBlock::Create(generator.get_context(), "ifcont", func);

  generator.get_builder().CreateCondBr(cond_val, then_bb, else_bb);

  // for the builder we cannot access with getBasicBlockList, first create with
  // BasicBlock::Create then insert into fns with Function::getEntryBlock()

  generator.get_builder().SetInsertPoint(then_bb);

  llvm::Value* then_val = then_branch->codegen(generator);
  if (!then_val) {
    throw codegen_error("invalid then branch in if expression");
  }

  generator.get_builder().CreateBr(merge_bb);
  then_bb = generator.get_builder().GetInsertBlock();

  generator.get_builder().SetInsertPoint(else_bb);

  llvm::Value* else_val = else_branch->codegen(generator);
  if (!else_val) {
    throw codegen_error("invalid else branch in if expression");
  }

  generator.get_builder().CreateBr(merge_bb);
  else_bb = generator.get_builder().GetInsertBlock();

  generator.get_builder().SetInsertPoint(merge_bb);

  llvm::PHINode* pn =
      generator.get_builder().CreatePHI(then_val->getType(), 2, "iftmp");

  pn->addIncoming(then_val, then_bb);
  pn->addIncoming(else_val, else_bb);

  return pn;
}

llvm::Value* def_codegen::codegen(llvm_codegen& generator) {
  std::vector<std::string> param_type_names;
  std::vector<std::string> param_names;

  for (const auto& param : params) {
    param_type_names.push_back(param.type_name);
    param_names.push_back(param.name);
  }

  auto type_info =
      generator.get_function_type_info(return_type_name, param_type_names);

  llvm::FunctionType* func_type = type_info.create_function_type();

  llvm::Function* func = llvm::Function::Create(
      func_type, llvm::Function::ExternalLinkage, name, generator.get_module());

  generator.get_current_scope()->set_function(name, func);

  unsigned idx = 0;
  for (auto& arg : func->args()) {
    arg.setName(param_names[idx++]);
  }

  llvm::BasicBlock* entry_bb =
      llvm::BasicBlock::Create(generator.get_context(), "entry", func);
  generator.get_builder().SetInsertPoint(entry_bb);

  auto function_scope = generator.create_new_scope();
  generator.set_current_scope(function_scope);

  idx = 0;
  for (auto& arg : func->args()) {
    llvm::AllocaInst* alloca = generator.create_entry_block_alloca(
        func, arg.getName().str(), arg.getType());

    generator.get_builder().CreateStore(&arg, alloca);

    function_scope->set_value(arg.getName().str(), alloca);
    idx++;
  }

  llvm::Value* body_val = body->codegen(generator);

  if (body_val) {
    generator.get_builder().CreateRet(body_val);

    llvm::verifyFunction(*func);
  } else {
    func->eraseFromParent();
    throw codegen_error("invalid function body");
  }

  generator.set_current_scope(generator.get_current_scope()->create_child());

  return func;
}

llvm::Value* call_codegen::codegen(llvm_codegen& generator) {
  llvm::Function* callee = generator.get_current_scope()->get_function(name);

  if (!callee) {
    throw codegen_error("unknown function: " + name);
  }

  if (callee->arg_size() != args.size()) {
    throw codegen_error("incorrect number of arguments passed to function: " +
                        name);
  }

  std::vector<llvm::Value*> arg_values;
  for (const auto& arg : args) {
    arg_values.push_back(arg->codegen(generator));
  }

  return generator.get_builder().CreateCall(callee, arg_values, "calltmp");
}

llvm::Value* binary_op_codegen::codegen(llvm_codegen& generator) {
  llvm::Value* l = lhs->codegen(generator);
  llvm::Value* r = rhs->codegen(generator);

  if (!l || !r) {
    throw codegen_error("invalid operands for binary operator");
  }

  if (op == TOKEN_ADD) {
    return generator.get_builder().CreateAdd(l, r, "addtmp");
  } else if (op == TOKEN_SUB) {
    return generator.get_builder().CreateSub(l, r, "subtmp");
  } else if (op == TOKEN_MUL) {
    return generator.get_builder().CreateMul(l, r, "multmp");
  } else if (op == TOKEN_DIV) {
    return generator.get_builder().CreateSDiv(l, r, "divtmp");
  } else if (op == TOKEN_EQ) {
    return generator.get_builder().CreateICmpEQ(l, r, "eqtmp");
  } else if (op == TOKEN_NEQ) {
    return generator.get_builder().CreateICmpNE(l, r, "netmp");
  } else if (op == TOKEN_LT) {
    return generator.get_builder().CreateICmpSLT(l, r, "lttmp");
  } else if (op == TOKEN_GT) {
    return generator.get_builder().CreateICmpSGT(l, r, "gttmp");
  } else if (op == TOKEN_LEQ) {
    return generator.get_builder().CreateICmpSLE(l, r, "letmp");
  } else if (op == TOKEN_GEQ) {
    return generator.get_builder().CreateICmpSGE(l, r, "getmp");
  } else if (op == "and") {
    return generator.get_builder().CreateAnd(l, r, "andtmp");
  } else if (op == "or") {
    return generator.get_builder().CreateOr(l, r, "ortmp");
  }

  throw codegen_error("unknown binary operator: " + op);
}

void llvm_codegen::initialize_intrinsics() {
  llvm::Type* void_type = llvm::Type::getVoidTy(*context);
  llvm::Type* int32_type = llvm::Type::getInt32Ty(*context);
  llvm::Type* int8_ptr_type = llvm::Type::getInt8PtrTy(*context);

  llvm::FunctionType* printf_type =
      llvm::FunctionType::get(int32_type, {int8_ptr_type}, true);

  llvm::Function* printf_func = llvm::Function::Create(
      printf_type, llvm::Function::ExternalLinkage, "printf", *module);

  intrinsic_functions["printf"] = printf_func;

  llvm::FunctionType* malloc_type =
      llvm::FunctionType::get(int8_ptr_type, {int32_type}, false);

  llvm::Function* malloc_func = llvm::Function::Create(
      malloc_type, llvm::Function::ExternalLinkage, "malloc", *module);

  intrinsic_functions["malloc"] = malloc_func;

  llvm::FunctionType* free_type =
      llvm::FunctionType::get(void_type, {int8_ptr_type}, false);

  llvm::Function* free_func = llvm::Function::Create(
      free_type, llvm::Function::ExternalLinkage, "free", *module);

  intrinsic_functions["free"] = free_func;
}

llvm::Function* llvm_codegen::get_intrinsic(const std::string& name) {
  auto it = intrinsic_functions.find(name);
  if (it != intrinsic_functions.end()) {
    return it->second;
  }

  return nullptr;
}

function_type_info llvm_codegen::get_function_type_info(
    const std::string& return_type,
    const std::vector<std::string>& param_types) {
  function_type_info result;

  result.return_type = get_llvm_type(return_type);

  for (const auto& param_type : param_types) {
    result.param_types.push_back(get_llvm_type(param_type));
  }

  return result;
}

void llvm_codegen::emit_to_file(const std::string& filename) {
  std::string str;
  llvm::raw_string_ostream stream(str);

  stream << *module;
  stream.flush();

  // @fix LLVM_LIBS bitwriter & support not linking issue
  // write string to file using standard C++ file I/O instead
  std::ofstream outfile(filename, std::ios::out | std::ios::binary);

  if (!outfile) {
    throw codegen_error("could not open file: " + filename);
  }

  outfile.write(str.c_str(), str.size());
  outfile.close();
}

void llvm_codegen::emit_bitcode(const std::string& filename) {
  std::string buffer;
  llvm::raw_string_ostream stream(buffer);

  // write bitcode to memory buffer
  // llvm::WriteBitcodeToFile(*module, stream);
  stream.flush();

  std::ofstream outfile(filename, std::ios::out | std::ios::binary);

  if (!outfile) {
    throw codegen_error("could not open file: " + filename);
  }

  outfile.write(buffer.c_str(), buffer.size());
  outfile.close();
}

void llvm_codegen::dump_ir() { module->print(llvm::outs(), nullptr); }

std::shared_ptr<node_codegen> codegen_visitor::codegen_node(
    const std::shared_ptr<typed_lisp::node>& node) {
  if (auto atom_node = std::dynamic_pointer_cast<typed_lisp::atom>(node)) {
    return std::make_shared<atom_codegen>(atom_node->value);
  } else if (auto list_node =
                 std::dynamic_pointer_cast<typed_lisp::list>(node)) {
    if (list_node->children.empty()) {
      return std::make_shared<list_codegen>(
          std::vector<std::shared_ptr<node_codegen>>{});
    }

    auto first =
        std::dynamic_pointer_cast<typed_lisp::atom>(list_node->children[0]);

    if (!first) {
      throw codegen_error("first element of list must be an atom");
    }

    if (first->value == TOKEN_LET) {
      if (list_node->children.size() != 5) {
        throw codegen_error("invalid let expression");
      }

      auto name_node =
          std::dynamic_pointer_cast<typed_lisp::atom>(list_node->children[1]);
      auto colon =
          std::dynamic_pointer_cast<typed_lisp::atom>(list_node->children[2]);
      auto type_node =
          std::dynamic_pointer_cast<typed_lisp::atom>(list_node->children[3]);

      if (!name_node || !colon || !type_node || colon->value != TOKEN_COLON) {
        throw codegen_error("invalid let syntax");
      }

      auto value_codegen = codegen_node(list_node->children[4]);

      return std::make_shared<let_codegen>(name_node->value, type_node->value,
                                           value_codegen);
    } else if (first->value == TOKEN_SET) {
      if (list_node->children.size() != 3) {
        throw codegen_error("invalid set expression");
      }

      auto name_node =
          std::dynamic_pointer_cast<typed_lisp::atom>(list_node->children[1]);

      if (!name_node) {
        throw codegen_error("invalid set syntax");
      }

      auto value_codegen = codegen_node(list_node->children[2]);

      return std::make_shared<set_codegen>(name_node->value, value_codegen);
    } else if (first->value == TOKEN_IF) {
      if (list_node->children.size() != 4) {
        throw codegen_error("invalid if expression");
      }

      // auto cond_codegen = codegen_node(list_node);
    }
  }
}
}  // namespace typed_lisp

int main() {
  // typed_lisp::type_system ty;
  // typed_lisp::type_env env;

  // auto int_type = ty.get_type(TYPE_INT);
  // auto bool_type = ty.get_type(TYPE_BOOL);
  // auto string_type = ty.get_type(TYPE_STRING);

  // // inst: (int -> bool)
  // auto pred_type = ty.make_function_type(int_type, bool_type);
  // env.insert("f", pred_type);

  // // check: ('a -> 'a)
  // auto type_var_a = ty.fresh_var();
  // auto identity_type = ty.make_function_type(type_var_a, type_var_a);
  // env.insert("identity", identity_type);

  // // check HOF: ((int -> bool) -> string)
  // auto higher_order = ty.make_function_type(
  //     ty.make_function_type(int_type, bool_type), string_type);
  // env.insert("describe_predicate", higher_order);

  // try {
  //   auto identity_int = ty.make_function_type(int_type, type_var_a);
  //   ty.unify(identity_type, identity_int);
  //   std::cout << "identity can take int: "
  //             << ty.get_final_type(identity_int)->to_string() << "\n";

  //   // check: predicate matches expected type
  //   auto test_pred = ty.make_function_type(int_type, bool_type);
  //   ty.unify(env.lookup("f"), test_pred);
  //   std::cout << "valid predicate type check\n";

  //   // (fails) trying to use string where int expected
  //   auto bad_pred = ty.make_function_type(string_type, bool_type);
  //   ty.unify(env.lookup("f"), bad_pred);

  // } catch (const std::runtime_error& e) {
  //   std::cout << "type error: " << e.what() << "\n";
  // }

  std::ifstream file("tests/valid-def-expr.lsp");
  std::string test_program((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());

  typed_lisp::lisp_parser parser(test_program);

  try {
    std::shared_ptr<typed_lisp::node> ast = parser.parse();
    auto visitor = std::make_shared<typed_lisp::type_visitor>(parser);

    /*@todo:fix*/ typed_lisp::register_builtins(visitor->global_scope);

    ast->accept(visitor.get());

    const auto& errors = visitor->get_errors();

    if (errors.empty()) {
      std::cout << "no type errors found!\n";
    } else {
      for (const auto& error : errors) {
        std::cout << error << "\n";
      }
    }

  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << std::endl;
    return 1;
  }

  // try {
  //   std::shared_ptr<typed_lisp::node> ast = parser.parse();
  //   auto visitor = std::make_shared<typed_lisp::codegen_visitor>();

  //   visitor->codegen_node(ast);

  // } catch (const std::exception& e) {
  //   std::cerr << "error: " << e.what() << std::endl;
  //   return 1;
  // }
}
