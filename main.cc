#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace typed_lisp {

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

    if (input[current_pos] == '(') {
      return parse_list();
    }

    return parse_atom();
  }

  std::shared_ptr<list> parse_list() {
    if (input[current_pos] != '(') {
      throw std::runtime_error("expected opening parenthesis");
    }

    current_pos++;

    auto lst = std::make_shared<list>();

    while (current_pos < input.length() && input[current_pos] != ')') {
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
           input[current_pos] != '(' && input[current_pos] != ')') {
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

  if (type_repr.size() > 0) {
    oss << "\n\n        " << "??" << "\n";
    oss << purple << "     —————————" << reset << " ... " << type_repr
        << " ∈ Γ without implication"
        << "\n";  // @todo: there needs to be flags for error kinds
    oss << purple << "      Γ ⊢ " << reset << type_repr << "\n";
    oss << "\n  constraint is unsatisfied unless deducing from opaque "
           "context.";
  }

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
    } catch (const std::runtime_error&) {
      if (parent) return parent->lookup_type(name);
      throw std::runtime_error("unbound variable: " + name);  // ??
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
    if (value == "true" || value == "false")
      return current_scope->get_type_system().get_type("bool");

    try {
      std::stoi(value);
      return current_scope->get_type_system().get_type("int");
    } catch (...) {
    }

    if (value.front() == '"' && value.back() == '"')
      return current_scope->get_type_system().get_type("string");

    if (value.front() == '\'') {
      auto var = current_scope->get_type_system().fresh_var();
      return var;
    }

    return current_scope->lookup_type(value);
  }

  type_ptr infer_binary_op(const std::string& op, type_ptr lhs, type_ptr rhs) {
    auto& ts = current_scope->get_type_system();

    try {
      ts.unify(lhs, rhs);

      if (op == "+" || op == "-" || op == "*" || op == "/") {
        auto int_t = ts.get_type("int");
        ts.unify(lhs, int_t);
        return int_t;
      }

      if (op == "=" || op == "!=" || op == "<" || op == ">") {
        return ts.get_type("bool");
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

    if (!name_node || !colon || !type_node || colon->value != ":") {
      std::shared_ptr<typed_lisp::node> shared_node = node->shared_from_this();
      with_error("malformed let expression", shared_node, nullptr,
                 "expected (let name : type value)");
      return;
    }

    std::vector<int> poly_vars;
    type_ptr declared_type;

    if (type_node->value.front() == '\'') {
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
        colon->value != ":") {
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

      if (!param_name || !param_colon || !param_type ||
          param_colon->value != ":") {
        errors.push_back("malformed parameter");
        continue;
      }

      type_ptr param_t;
      if (param_type->value.front() == '\'') {
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
    if (ret_type_node->value.front() == '\'') {
      auto var = current_scope->get_type_system().fresh_var();
      poly_vars.push_back(std::dynamic_pointer_cast<var_type>(var)->id);
      ret_t = var;
    } else {
      ret_t = current_scope->get_type_system().get_type(ret_type_node->value);
    }

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
      errors.push_back("type error in assignment: " + std::string(e.what()));
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
          cond_type, current_scope->get_type_system().get_type("bool"));
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
      errors.push_back("branches have different types: " +
                       std::string(e.what()));
    }
  }

  void visit_call(list* node) {
    if (node->children.empty()) return;

    auto fn = std::dynamic_pointer_cast<atom>(node->children[0]);
    if (!fn) {
      errors.push_back("expected function name");
      return;
    }

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

      current_scope->get_type_system().unify(fn_type, expected);
      current_type = result_type;
    } catch (const std::runtime_error& e) {
      errors.push_back("type error in function call: " + std::string(e.what()));
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

    if (fst->value == "let") {
      visit_let(node);
    } else if (fst->value == "def") {
      visit_def(node);
    } else if (fst->value == "set") {
      visit_set(node);
    } else if (fst->value == "if") {
      visit_if(node);
    } else {
      visit_call(node);
    }
  }

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
  auto int_t = ty.get_type("int");
  auto bool_t = ty.get_type("bool");
  auto string_t = ty.get_type("string");

  auto type_var_a = ty.fresh_var();
  auto type_var_b = ty.fresh_var();

  // we need better heuristics to register ops, i.e. never bind to concrete
  // types. additionally, skip the type specifier token instead of capturing

  scope->define_type(":", ty.make_function_type(type_var_a, type_var_b));
  scope->define_type("def", ty.make_function_type(type_var_a, type_var_b));
  scope->define_type("let", ty.make_function_type(type_var_a, type_var_b));
  scope->define_type("set", ty.make_function_type(type_var_a, type_var_b));
  scope->define_type("if", ty.make_function_type(type_var_a, type_var_b));
  // @fix: why does this fix the unbound issue?
  scope->define_type("int", ty.make_function_type(type_var_a, type_var_b));
  scope->define_type("n", ty.make_function_type(type_var_a, type_var_b));

  scope->define_type(
      "+", ty.make_function_type(int_t, ty.make_function_type(int_t, int_t)));
  scope->define_type(
      "-", ty.make_function_type(int_t, ty.make_function_type(int_t, int_t)));
  scope->define_type(
      "*", ty.make_function_type(int_t, ty.make_function_type(int_t, int_t)));
  scope->define_type(
      "/", ty.make_function_type(int_t, ty.make_function_type(int_t, int_t)));

  scope->define_type(
      "=", ty.make_function_type(int_t, ty.make_function_type(int_t, bool_t)));
  scope->define_type(
      ">", ty.make_function_type(int_t, ty.make_function_type(int_t, bool_t)));
  scope->define_type(
      "<", ty.make_function_type(int_t, ty.make_function_type(int_t, bool_t)));
}

}  // namespace typed_lisp

int main() {
  // typed_lisp::type_system ty;
  // typed_lisp::type_env env;

  // auto int_type = ty.get_type("int");
  // auto bool_type = ty.get_type("bool");
  // auto string_type = ty.get_type("string");

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

  std::ifstream file("tests/invalid-let-expr.lsp");
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
}
