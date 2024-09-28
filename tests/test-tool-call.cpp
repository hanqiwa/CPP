#include "tool-call.h"
#include "llama-grammar.h"
#include "unicode.h"

#include <fstream>
#include <iostream>
#include <string>
#include <json.hpp>

using json = nlohmann::ordered_json;

static void assert_equals(const std::string & expected, const std::string & actual) {
    if (expected != actual) {
        std::cerr << "Expected: " << expected << std::endl;
        std::cerr << "Actual: " << actual << std::endl;
        std::cerr << std::flush;
        throw std::runtime_error("Test failed");
    }
}

static std::string read_file(const std::string &path) {
  std::ifstream fs(path, std::ios_base::binary);
  if (!fs.is_open()) {
    fs = std::ifstream("../" + path, std::ios_base::binary);
    if (!fs.is_open()) {
      throw std::runtime_error("Failed to open file: " + path);
    }
  }
  fs.seekg(0, std::ios_base::end);
  auto size = fs.tellg();
  fs.seekg(0);
  std::string out;
  out.resize(static_cast<size_t>(size));
  fs.read(&out[0], static_cast<std::streamsize>(size));
  return out;
}

static llama_grammar * build_grammar(const std::string & grammar_str) {
    return llama_grammar_init_impl(nullptr, grammar_str.c_str(), "root");
}

// TODO: extract to common helper (copied from test-grammar-integration.cpp)
static bool match_string(const std::string & input, llama_grammar * grammar) {
    const auto cpts = unicode_cpts_from_utf8(input);

    const llama_grammar_rules  & rules      = llama_grammar_get_rules (grammar);
          llama_grammar_stacks & stacks_cur = llama_grammar_get_stacks(grammar);

    for (const auto & cpt : cpts) {
        const llama_grammar_stacks stacks_prev = llama_grammar_get_stacks(grammar); // copy

        llama_grammar_accept(rules, stacks_prev, cpt, stacks_cur);

        if (stacks_cur.empty()) {
            // no stacks means that the grammar failed to match at this point
            return false;
        }
    }

    for (const auto & stack : stacks_cur) {
        if (stack.empty()) {
            // An empty stack means that the grammar has been completed
            return true;
        }
    }

    return false;
}

// Dumps `{"a": 1}` as `"{\"a\": 1}"`, unlike nlohmann::json::dump which would dump it as `"{\"a\":1}"`.
static std::string dump(const json & j) {
  return minja::Value(j).dump(-1, /* to_json= */ true);
}

static void test_parse_tool_call(llama_tool_call_style style, const json & tools, const std::string & input, const std::string & expected_content, const json & expected_tool_calls) {
    std::cout << "# Testing: " << input << std::endl << std::flush;
    auto result = parse_tool_calls(style, tools, input);
    assert_equals(expected_content, result.content);
    auto tool_calls = json::array();
    for (const auto & tc : result.tool_calls) {
        tool_calls.push_back({
          {"type", "function"},
          {"function", {
            {"name", tc.name},
            {"arguments", dump(json::parse(tc.arguments))},
          }}
        });
    }
    auto expected = expected_tool_calls.dump();
    auto actual = tool_calls.dump();
    assert_equals(expected, actual);
}

const json tools = json::parse(R"([
  {
    "type": "function",
    "function": {
      "name": "special_function",
      "description": "I'm special",
      "parameters": {
        "type": "object",
        "properties": {
          "arg1": {
            "type": "integer",
            "description": "The arg."
          }
        },
        "required": ["arg1"]
      }
    }
  },
  {
    "type": "function",
    "function": {
      "name": "ipython",
      "description": "a python interpreter",
      "parameters": {
        "type": "object",
        "properties": {
          "code": {
            "type": "string",
            "description": "The code."
          }
        },
        "required": ["code"]
      }
    }
  }
])");

static void test_parsing() {
    json request = {
      {"tools", tools}
    };

    test_parse_tool_call(llama_tool_call_style::Hermes2Pro, tools,
      "<tool_call>{\"name\": \"foo\", \"arguments\": {\"bar\": 1}}</tool_call>",
      "",
      json {{
        {"type", "function"},
        {"function", {
          {"name", "foo"},
          {"arguments", dump({
            {"bar", 1}
          })}
        }}
      }});

    test_parse_tool_call(llama_tool_call_style::FunctionaryV3Llama3, tools,
      ">>>ipython\n{\"code\": \"print('Hello, world!')\"}",
      "",
      json {{
        {"type", "function"},
        {"function", {
          {"name", "ipython"},
          {"arguments", dump({
            {"code", "print('Hello, world!')"}
          })}
        }}
      }});
    test_parse_tool_call(llama_tool_call_style::FunctionaryV3Llama3, tools,
      ">>>special_function\n{\"arg1\": 1}\n ",
      "",
      json {{
        {"type", "function"},
        {"function", {
          {"name", "special_function"},
          {"arguments", dump({
            {"arg1", 1}
          })}
        }}
      }});

    test_parse_tool_call(llama_tool_call_style::FunctionaryV3Llama31, tools,
      "Hell<function=foo>{\"arg1\": 1}</function>o, world<function=bar>{\"arg2\": 2}</function>!",
      "Hello, world!",
      json {
        {
          {"type", "function"},
          {"function", {
            {"name", "foo"},
            {"arguments", dump({
              {"arg1", 1}
            })}
          }}
        },
        {
          {"type", "function"},
          {"function", {
            {"name", "bar"},
            {"arguments", dump({
              {"arg2", 2}
            })}
          }}
        },
      });
    test_parse_tool_call(llama_tool_call_style::FunctionaryV3Llama31, tools,
      "<function=test>{ } </function> ",
      " ",
      json {{
        {"type", "function"},
        {"function", {
          {"name", "test"},
          {"arguments", "{}"}
        }}
      }});

    test_parse_tool_call(llama_tool_call_style::Llama31, tools,
      "<|python_tag|>this could be anything",
      "",
      json {{
        {"type", "function"},
        {"function", {
          {"name", "ipython"},
          {"arguments", dump({
            {"code", "this could be anything"}
          })}
        }}
      }});
    test_parse_tool_call(llama_tool_call_style::Llama31, tools,
      "I'm thinking<|python_tag|>",
      "I'm thinking",
      json {{
        {"type", "function"},
        {"function", {
          {"name", "ipython"},
          {"arguments", dump({{"code", ""}})}
        }}
      }});
    test_parse_tool_call(llama_tool_call_style::Llama31, tools,
      "{\"name\": \"special_function\", \"parameters\": {\"arg1\": 1}}",
      "",
      json {{
        {"type", "function"},
        {"function", {
          {"name", "special_function"},
          {"arguments", dump({{"arg1", 1}})}
        }}
      }});
    test_parse_tool_call(llama_tool_call_style::Llama31, tools,
      "{\"name\": \"unknown_function\", \"arguments\": {\"arg1\": 1}}",
      "{\"name\": \"unknown_function\", \"arguments\": {\"arg1\": 1}}", json::array());
}

static std::string get_message_prompt_delta(const llama_chat_template & tmpl, const std::vector<std::string> & end_tokens, const json & user_message, const json & delta_message, const json & tools) {
  auto prefix = tmpl.apply(json::array({user_message}), tools, /* add_generation_prompt= */ true, json::object());
  auto full = tmpl.apply(json::array({user_message, delta_message}), tools, /* add_generation_prompt= */ false, json::object());

  // Check full starts with prefix
  if (full.find(prefix) != 0) {
    throw std::runtime_error("Full message does not start with prefix");
  }

  auto delta = full.substr(prefix.size());

  // Strip end tokens
  for (const auto & end_token : end_tokens) {
    // rfind to find the last occurrence
    auto pos = delta.rfind(end_token);
    if (pos != std::string::npos) {
      delta = delta.substr(0, pos);
      break;
    }
  }
  return delta;
}

static void test_template(const std::string & template_file, const char * bos_token, const char * eos_token, const std::vector<std::string> & end_tokens, const json & tool_calling_message, const json & tools) {
  std::cout << "# Testing template: " << template_file << std::endl << std::flush;
  const llama_chat_template & tmpl = llama_chat_template(read_file(template_file), bos_token, eos_token);
  auto & tool_calls = tool_calling_message.at("tool_calls");

  // Format the message: apply the template to 1 user message w/ add_generation_prompt=true, then w/ the extra message w/ add_generation_prompt=false,
  // get the diff and try and parse it w/ the grammar.
  auto user_message = json {
      {"role", "user"},
      {"content", "Hello, world!"}
  };

  auto handler = llama_tool_call_handler_init(tmpl, /* allow_content= */ true, /* parallel_tool_calls= */ true, {user_message, tool_calling_message}, tools);
  auto grammar = build_grammar(handler.grammar);
  if (!grammar) {
    throw std::runtime_error("Failed to build grammar");
  }

  auto full_delta = get_message_prompt_delta(tmpl, end_tokens, user_message, tool_calling_message, tools);
  std::cout << "Full delta:\n```\n" << full_delta << "\n```" << std::endl;
  test_parse_tool_call(tmpl.tool_call_style(), tools, full_delta, "", tool_calls);

  auto content_less_delta = get_message_prompt_delta(tmpl, end_tokens, user_message, {
    {"role", "assistant"},
    {"content", ""},
    {"tool_calls", tool_calls}
  }, tools);
  if (!match_string(content_less_delta, grammar)) {
    throw std::runtime_error("Failed to match content-less delta against grammar:\n\nContent-less delta: " + content_less_delta + "\n\nGrammar: " + handler.grammar);
  }
}

static void test_grammars() {
  auto tool_call_message = json {
    {"role", "assistant"},
    {"content", ""},
    {"tool_calls", json {{
      {"type", "function"},
      {"function", {
        {"name", "special_function"},
        {"arguments", "{\"arg1\": 1}"}
      }}
    }}}
  };
  test_template("tests/chat/templates/NousResearch-Hermes-2-Pro-Llama-3-8B-tool_use.jinja", "<s>", "</s>", { "<|im_end|>" }, tool_call_message, tools);
  test_template("tests/chat/templates/meta-llama-Meta-Llama-3.1-8B-Instruct.jinja", "<s>", "</s>", { "<|eom_id|>", "<|eot_id|>" }, tool_call_message, tools);
  test_template("tests/chat/templates/meta-llama-Llama-3.2-3B-Instruct.jinja", "<s>", "</s>", { "<|eom_id|>", "<|eot_id|>" }, tool_call_message, tools);
  test_template("tests/chat/templates/meetkai-functionary-medium-v3.1.jinja", "<s>", "</s>", { "<|eom_id|>", "<|eot_id|>" }, tool_call_message, tools);
  test_template("tests/chat/templates/meetkai-functionary-medium-v3.2.jinja", "<s>", "</s>", { "<|eom_id|>", "<|eot_id|>" }, tool_call_message, tools);
}

int main() {
    test_grammars();
    test_parsing();

    std::cout << "[tool-call] All tests passed!" << std::endl;
    return 0;
}
