#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace typed_lisp {
class lisp_node {
 public:
  virtual ~lisp_node() = default;
  virtual void accept(class node_visitor* visitor) = 0;
};

class atom_node : public lisp_node {
 public:
  std::string value;

  explicit atom_node(std::string val) : value(std::move(val)) {}

  void accept(node_visitor* visitor) override;
};

class list_node : public lisp_node {
 public:
  std::vector<std::shared_ptr<lisp_node>> children;

  void accept(node_visitor* visitor) override;
};

class node_visitor {
 public:
  virtual void visit(atom_node* node) = 0;
  virtual void visit(list_node* node) = 0;
  virtual ~node_visitor() = default;
};

void atom_node::accept(node_visitor* visitor) { visitor->visit(this); }

void list_node::accept(node_visitor* visitor) {
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

  std::shared_ptr<lisp_node> parse_expression() {
    skip_whitespace();

    if (current_pos >= input.length()) {
      throw std::runtime_error("unexpected end of input");
    }

    if (input[current_pos] == '(') {
      return parse_list();
    }

    return parse_atom();
  }

  std::shared_ptr<list_node> parse_list() {
    if (input[current_pos] != '(') {
      throw std::runtime_error("expected opening parenthesis");
    }
    current_pos++;

    auto list = std::make_shared<list_node>();

    while (current_pos < input.length() && input[current_pos] != ')') {
      list->children.push_back(parse_expression());
      skip_whitespace();
    }

    if (current_pos >= input.length()) {
      throw std::runtime_error("unclosed list");
    }

    current_pos++;

    return list;
  }

  std::shared_ptr<atom_node> parse_atom() {
    size_t start = current_pos;

    while (current_pos < input.length() && !std::isspace(input[current_pos]) &&
           input[current_pos] != '(' && input[current_pos] != ')') {
      current_pos++;
    }

    std::string value = input.substr(start, current_pos - start);

    return std::make_shared<atom_node>(value);
  }

 public:
  explicit lisp_parser(std::string input_str) : input(std::move(input_str)) {}

  std::shared_ptr<lisp_node> parse() {
    current_pos = 0;

    return parse_expression();
  }
};
}  // namespace typed_lisp

// === tests ===

class tokens_print_visitor : public typed_lisp::node_visitor {
 public:
  void visit(typed_lisp::atom_node* node) override {
    std::cout << node->value << " ";
  }

  void visit(typed_lisp::list_node* node) override { std::cout << "( "; }
};

int main() {
  std::string input = "(+ 1 (* 2 3) 4)";
  typed_lisp::lisp_parser parser(input);

  try {
    std::shared_ptr<typed_lisp::lisp_node> root = parser.parse();
    tokens_print_visitor visitor;
    root->accept(&visitor);
  } catch (const std::exception& e) {
    std::cerr << "Parsing error: " << e.what() << std::endl;
  }

  return 0;
}
