#ifdef NDEBUG
#undef NDEBUG
#endif

#include <iostream>
#include <fstream>
#include <sstream>
#include <regex>

// TODO: split this to some library? common would need json.hpp
#include "../examples/server/json-schema-to-grammar.h"
#include "../examples/server/json-schema-to-grammar.cpp"

using namespace std;

static std::string trim(const std::string & source) {
    std::string s(source);
    s.erase(0,s.find_first_not_of(" \n\r\t"));
    s.erase(s.find_last_not_of(" \n\r\t")+1);
    return regex_replace(s, regex("(^|\n)[ \t]+"), "$1");
}

struct TestCase {
  string name;
  string schema;
  string expected;

  void write_input() const {
    ofstream f;
    f.open("test-json-schema-input.tmp");
    f << schema.c_str();
    f.close();
  }

  void read_and_verify(const string& series) const {
    ostringstream actuals;
    actuals << ifstream("test-grammar-output.tmp").rdbuf();
    auto actual = actuals.str();
    verify(series, actual);
  }

  void verify(const string& series, const string& actual) const {
    if (trim(actual) != trim(expected)) {
      cerr << "#" << endl;
      cerr << "# Test " << name.c_str() << " (" << series.c_str() << ") failed." << endl;
      cerr << "#" << endl;
      cerr << schema.c_str() << endl;
      cerr << "# EXPECTED:\n" << expected.c_str() << endl;
      cerr << "# ACTUAL:\n" << actual.c_str() << endl;
      assert(false);
    }
  }

};


static void run_py(const TestCase& tc) {
  cerr << "Testing JSON schema conversion: " << tc.name.c_str() << " (Python)" << endl;
  tc.write_input();
  assert(std::system("python ./examples/json-schema-to-grammar.py test-json-schema-input.tmp > test-grammar-output.tmp") == 0);
  tc.read_and_verify("Python");
}

static void run_mjs(const TestCase& tc) {
  cerr << "Testing JSON schema conversion: " << tc.name.c_str() << " (JS)" << endl;
  tc.write_input();
  assert(std::system("node ./tests/run-json-schema-to-grammar.mjs test-json-schema-input.tmp > test-grammar-output.tmp") == 0);
  tc.read_and_verify("JavaScript");
}

static void run_cpp(const TestCase& tc) {
  cerr << "Testing JSON schema conversion: " << tc.name.c_str() << " (C++)" << endl;
  auto actual = json_schema_to_grammar(nlohmann::json::parse(tc.schema));
  tc.verify("C++", actual);
}

static void run_all(const TestCase& tc) {
  run_py(tc);
  run_mjs(tc);
  run_cpp(tc);
}

int main() {
  run_all({
    "exotic formats",
    R"""({
      "items": [
        { "format": "date" },
        { "format": "uuid" },
        { "format": "time" },
        { "format": "date-time" }
      ]
    })""",
    R"""(
      date ::= [0-9] [0-9] [0-9] [0-9] "-" ( "0" [1-9] | "1" [0-2] ) "-" ( [0-2] [0-9] | "3" [0-1] )
      date-string ::= "\"" date "\"" space
      date-time ::= date "T" time
      date-time-string ::= "\"" date-time "\"" space
      root ::= "[" space date-string "," space uuid "," space time-string "," space date-time-string "]" space
      space ::= " "?
      time ::= ([01] [0-9] | "2" [0-3]) ":" [0-5] [0-9] ":" [0-5] [0-9] ( "." [0-9] [0-9] [0-9] )? ( "Z" | ( "+" | "-" ) ( [01] [0-9] | "2" [0-3] ) ":" [0-5] [0-9] )
      time-string ::= "\"" time "\"" space
      uuid ::= "\"" [0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F] "-" [0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F] "-" [0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F] "-" [0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F] "-" [0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F] "\"" space
    )"""
  });

  run_all({
    "string",
    R"""({
      "type": "string"
    })""",
    R"""(
      root ::=  "\"" (
              [^"\\] |
              "\\" (["\\/bfnrt] | "u" [0-9a-fA-F] [0-9a-fA-F] [0-9a-fA-F] [0-9a-fA-F])
            )* "\"" space
      space ::= " "?
    )"""
  });

  run_all({
    "boolean",
    R"""({
      "type": "boolean"
    })""",
    R"""(
      root ::= ("true" | "false") space
      space ::= " "?
    )"""
  });

  run_all({
    "integer",
    R"""({
      "type": "integer"
    })""",
    R"""(
      root ::= ("-"? ([0-9] | [1-9] [0-9]*)) space
      space ::= " "?
    )"""
  });

  run_all({
    "tuple1",
    R"""({
      "prefixItems": [{ "type": "string" }]
    })""",
    R"""(
      root ::= "[" space string "]" space
      space ::= " "?
      string ::=  "\"" (
              [^"\\] |
              "\\" (["\\/bfnrt] | "u" [0-9a-fA-F] [0-9a-fA-F] [0-9a-fA-F] [0-9a-fA-F])
            )* "\"" space
    )"""
  });

  run_all({
    "tuple2",
    R"""({
      "prefixItems": [{ "type": "string" }, { "type": "number" }]
    })""",
    R"""(
      number ::= ("-"? ([0-9] | [1-9] [0-9]*)) ("." [0-9]+)? ([eE] [-+]? [0-9]+)? space
      root ::= "[" space string "," space number "]" space
      space ::= " "?
      string ::=  "\"" (
              [^"\\] |
              "\\" (["\\/bfnrt] | "u" [0-9a-fA-F] [0-9a-fA-F] [0-9a-fA-F] [0-9a-fA-F])
            )* "\"" space
    )"""
  });

  run_all({
    "number",
    R"""({
      "type": "number"
    })""",
    R"""(
      root ::= ("-"? ([0-9] | [1-9] [0-9]*)) ("." [0-9]+)? ([eE] [-+]? [0-9]+)? space
      space ::= " "?
    )"""
  });

  run_all({
    "minItems",
    R"""({
      "items": {
        "type": "boolean"
      },
      "minItems": 2
    })""",
    R"""(
      boolean ::= ("true" | "false") space
      root ::= "[" space boolean ( "," space boolean )( "," space boolean )* "]" space
      space ::= " "?
    )"""
  });

  run_all({
    "maxItems 1",
    R"""({
      "items": {
        "type": "boolean"
      },
      "maxItems": 1
    })""",
    R"""(
      boolean ::= ("true" | "false") space
      root ::= "[" space ( boolean  )? "]" space
      space ::= " "?
    )"""
  });

  run_all({
    "maxItems 2",
    R"""({
      "items": {
        "type": "boolean"
      },
      "maxItems": 2
    })""",
    R"""(
      boolean ::= ("true" | "false") space
      root ::= "[" space ( boolean ( "," space boolean )? )? "]" space
      space ::= " "?
    )"""
  });

  run_all({
    "min + maxItems",
    R"""({
      "items": {
        "type": ["number", "integer"]
      },
      "minItems": 3,
      "maxItems": 5
    })""",
    R"""(
      integer ::= ("-"? ([0-9] | [1-9] [0-9]*)) space
      item ::= number | integer
      number ::= ("-"? ([0-9] | [1-9] [0-9]*)) ("." [0-9]+)? ([eE] [-+]? [0-9]+)? space
      root ::= "[" space item ( "," space item )( "," space item )( "," space item )?( "," space item )? "]" space
      space ::= " "?
    )"""
  });

  run_all({
    "regexp",
    R"""({
      "type": "string",
      "pattern": "^(\\([0-9]{1,3}\\))?[0-9]{3}-[0-9]{4} and...$"
    })""",
    R"""(
      dot ::= [\U00000000-\x09\x0B\x0C\x0E-\U0010FFFF]
      root ::= ("(" root-1 root-1? root-1? ")")? root-1 root-1 root-1 "-" root-1 root-1 root-1 root-1 " and" dot dot dot
      root-1 ::= [0-9]
      space ::= " "?
    )"""
  });

  run_all({
    "object w/ required props",
    R"""({
      "type": "object",
      "properties": {
        "a": {
          "type": "string"
        },
        "b": {
          "type": "string"
        }
      },
      "required": [
        "a",
        "b"
      ],
      "additionalProperties": false,
      "definitions": {}
    })""",
    R"""(
      a-kv ::= "\"a\"" space ":" space string
      b-kv ::= "\"b\"" space ":" space string
      root ::= "{" space a-kv "," space b-kv "}" space
      space ::= " "?
      string ::=  "\"" (
              [^"\\] |
              "\\" (["\\/bfnrt] | "u" [0-9a-fA-F] [0-9a-fA-F] [0-9a-fA-F] [0-9a-fA-F])
            )* "\"" space
    )"""
  });

  run_all({
    "1 optional",
    R"""({
      "properties": {
        "a": {
          "type": "string"
        }
      },
      "additionalProperties": false
    })""",
    R"""(
      a-kv ::= "\"a\"" space ":" space string
      root ::= "{" space  (a-kv )? "}" space
      space ::= " "?
      string ::=  "\"" (
              [^"\\] |
              "\\" (["\\/bfnrt] | "u" [0-9a-fA-F] [0-9a-fA-F] [0-9a-fA-F] [0-9a-fA-F])
            )* "\"" space
    )"""
  });

  run_all({
    "optionals",
    R"""({
      "type": "object",
      "properties": {
        "a": {
          "type": "string"
        },
        "b": {
          "type": "string"
        },
        "c": {
          "type": [
            "number",
            "string"
          ]
        },
        "d": {
          "type": "string"
        },
        "e": {
          "type": "object",
          "additionalProperties": {
            "type": "array",
            "items": {
              "type": "array",
              "minItems": 2,
              "items": [
                {
                  "type": "string"
                },
                {
                  "type": "number"
                }
              ],
              "maxItems": 2
            }
          }
        }
      },
      "required": [
        "a",
        "b"
      ],
      "additionalProperties": false,
      "definitions": {}
    })""",
    R"""(
      a-kv ::= "\"a\"" space ":" space string
      b-kv ::= "\"b\"" space ":" space string
      c ::= number | string
      c-kv ::= "\"c\"" space ":" space c
      c-rest ::= ( "," space d-kv )? d-rest
      d-kv ::= "\"d\"" space ":" space string
      d-rest ::= ( "," space e-kv )?
      e ::= ( e-additionalProperties-kv ( "," space e-additionalProperties-kv )* )*
      e-additionalProperties-kv ::= string ":" space e-additionalProperties-value
      e-additionalProperties-value ::= "[" space ( e-additionalProperties-value-item ( "," space e-additionalProperties-value-item )* )? "]" space
      e-additionalProperties-value-item ::= "[" space string "," space number "]" space
      e-kv ::= "\"e\"" space ":" space e
      number ::= ("-"? ([0-9] | [1-9] [0-9]*)) ("." [0-9]+)? ([eE] [-+]? [0-9]+)? space
      root ::= "{" space a-kv "," space b-kv ( "," space ( c-kv c-rest | d-kv d-rest | e-kv ) )? "}" space
      space ::= " "?
      string ::=  "\"" (
              [^"\\] |
              "\\" (["\\/bfnrt] | "u" [0-9a-fA-F] [0-9a-fA-F] [0-9a-fA-F] [0-9a-fA-F])
            )* "\"" space
    )"""
  });

  run_all({
    "top-level $ref",
    R"""({
      "$ref": "#/definitions/MyType",
      "definitions": {
        "MyType": {
          "type": "object",
          "properties": {
            "a": {
              "type": "string"
            }
          },
          "required": [
            "a"
          ],
          "additionalProperties": false
        }
      }
    })""",
    R"""(
      MyType ::= "{" space MyType-a-kv "}" space
      MyType-a-kv ::= "\"a\"" space ":" space string
      root ::= MyType
      space ::= " "?
      string ::=  "\"" (
              [^"\\] |
              "\\" (["\\/bfnrt] | "u" [0-9a-fA-F] [0-9a-fA-F] [0-9a-fA-F] [0-9a-fA-F])
            )* "\"" space
    )"""
  });

  run_all({
    "conflicting names",
    R"""({
      "type": "object",
      "properties": {
        "number": {
          "type": "object",
          "properties": {
            "number": {
              "type": "object",
              "properties": {
                "root": {
                  "type": "number"
                }
              },
              "required": [
                "root"
              ],
              "additionalProperties": false
            }
          },
          "required": [
            "number"
          ],
          "additionalProperties": false
        }
      },
      "required": [
        "number"
      ],
      "additionalProperties": false,
      "definitions": {}
    })""",
    R"""(
      number ::= ("-"? ([0-9] | [1-9] [0-9]*)) ("." [0-9]+)? ([eE] [-+]? [0-9]+)? space
      number- ::= "{" space number-number-kv "}" space
      number-kv ::= "\"number\"" space ":" space number-
      number-number ::= "{" space number-number-root-kv "}" space
      number-number-kv ::= "\"number\"" space ":" space number-number
      number-number-root-kv ::= "\"root\"" space ":" space number
      root ::= "{" space number-kv "}" space
      space ::= " "?
    )"""
  });
}
