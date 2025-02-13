#include <algorithm>
#include <iostream>
#include <memory>
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

class list : public node {
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

  void skip_whitespace() {
    while (current_pos < input.length() && std::isspace(input[current_pos])) {
      current_pos++;
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
};

class tokens_print_visitor : public typed_lisp::node_visitor {
 public:
  void visit(typed_lisp::atom* node) override {
    std::cout << node->value << " ";
  }

  void visit(typed_lisp::list* node) override {
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

    throw std::runtime_error("type mismatch\nexpected " + t1->to_string() +
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

}  // namespace typed_lisp

int main() {
  typed_lisp::type_system ty;
  typed_lisp::type_env env;

  auto int_type = ty.get_type("int");
  auto bool_type = ty.get_type("bool");
  auto string_type = ty.get_type("string");

  // inst: (int -> bool)
  auto pred_type = ty.make_function_type(int_type, bool_type);
  env.insert("f", pred_type);

  // check: ('a -> 'a)
  auto type_var_a = ty.fresh_var();
  auto identity_type = ty.make_function_type(type_var_a, type_var_a);
  env.insert("identity", identity_type);

  // check HOF: ((int -> bool) -> string)
  auto higher_order = ty.make_function_type(
      ty.make_function_type(int_type, bool_type), string_type);
  env.insert("describe_predicate", higher_order);

  try {
    auto identity_int = ty.make_function_type(int_type, type_var_a);
    ty.unify(identity_type, identity_int);
    std::cout << "identity can take int: "
              << ty.get_final_type(identity_int)->to_string() << "\n";

    // check: predicate matches expected type
    auto test_pred = ty.make_function_type(int_type, bool_type);
    ty.unify(env.lookup("f"), test_pred);
    std::cout << "valid predicate type check\n";

    // (fails) trying to use string where int expected
    auto bad_pred = ty.make_function_type(string_type, bool_type);
    ty.unify(env.lookup("f"), bad_pred);

  } catch (const std::runtime_error& e) {
    std::cout << "type error: " << e.what() << "\n";
  }

  // revisit this stuff when there is a wrapper context for types & modules

  // std::string input = R"(
  //   (def fact : Int (n: Int)
  //     (if (= n 0)
  //         1
  //         (* n (fact (- n 1)))))
  // )";

  // typed_lisp::lisp_parser parser(input);

  // try {
  //   std::shared_ptr<typed_lisp::node> root = parser.parse();
  //   typed_lisp::tokens_print_visitor visitor;
  //   root->accept(&visitor);
  // } catch (const std::exception& e) {
  //   std::cerr << "parsing error: " << e.what() << std::endl;
  // }

  return 0;
}
