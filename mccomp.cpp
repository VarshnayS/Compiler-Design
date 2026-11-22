#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <queue>
#include <string.h>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using namespace llvm;
using namespace llvm::sys;

FILE *pFile;

//===----------------------------------------------------------------------===//
// Lexer
//===----------------------------------------------------------------------===//

// The lexer returns one of these for known things.
enum TOKEN_TYPE {

  IDENT = -1,        // [a-zA-Z_][a-zA-Z_0-9]*
  ASSIGN = int('='), // '='

  // delimiters
  LBRA = int('{'),  // left brace
  RBRA = int('}'),  // right brace
  LPAR = int('('),  // left parenthesis
  LBOX = int('['),  // left bracket
  RBOX = int(']'),  // right bracket
  RPAR = int(')'),  // right parenthesis
  SC = int(';'),    // semicolon
  COMMA = int(','), // comma

  // types
  INT_TOK = -2,   // "int"
  VOID_TOK = -3,  // "void"
  FLOAT_TOK = -4, // "float"
  BOOL_TOK = -5,  // "bool"

  // keywords
  EXTERN = -6,  // "extern"
  IF = -7,      // "if"
  ELSE = -8,    // "else"
  WHILE = -9,   // "while"
  RETURN = -10, // "return"
  // TRUE   = -12,     // "true"
  // FALSE   = -13,     // "false"

  // literals
  INT_LIT = -14,   // [0-9]+
  FLOAT_LIT = -15, // [0-9]+.[0-9]+
  BOOL_LIT = -16,  // "true" or "false" key words

  // logical operators
  AND = -17, // "&&"
  OR = -18,  // "||"

  // operators
  PLUS = int('+'),    // addition or unary plus
  MINUS = int('-'),   // substraction or unary negative
  ASTERIX = int('*'), // multiplication
  DIV = int('/'),     // division
  MOD = int('%'),     // modular
  NOT = int('!'),     // unary negation

  // comparison operators
  EQ = -19,      // equal
  NE = -20,      // not equal
  LE = -21,      // less than or equal to
  LT = int('<'), // less than
  GE = -23,      // greater than or equal to
  GT = int('>'), // greater than

  // special tokens
  EOF_TOK = 0, // signal end of file

  // invalid
  INVALID = -100 // signal invalid token
};

// TOKEN class is used to keep track of information about a token
class TOKEN {
public:
  TOKEN() = default;
  int type = -100;
  std::string lexeme;
  int lineNo;
  int columnNo;
  const std::string getIdentifierStr() const;
  const int getIntVal() const;
  const float getFloatVal() const;
  const bool getBoolVal() const;
};

static std::string globalLexeme;
static int lineNo, columnNo;

const std::string TOKEN::getIdentifierStr() const {
  if (type != IDENT) {
    fprintf(stderr, "%d:%d Error: %s\n", lineNo, columnNo,
            "getIdentifierStr called on non-IDENT token");
    exit(2);
  }
  return lexeme;
}

const int TOKEN::getIntVal() const {
  if (type != INT_LIT) {
    fprintf(stderr, "%d:%d Error: %s\n", lineNo, columnNo,
            "getIntVal called on non-INT_LIT token");
    exit(2);
  }
  return strtod(lexeme.c_str(), nullptr);
}

const float TOKEN::getFloatVal() const {
  if (type != FLOAT_LIT) {
    fprintf(stderr, "%d:%d Error: %s\n", lineNo, columnNo,
            "getFloatVal called on non-FLOAT_LIT token");
    exit(2);
  }
  return strtof(lexeme.c_str(), nullptr);
}

const bool TOKEN::getBoolVal() const {
  if (type != BOOL_LIT) {
    fprintf(stderr, "%d:%d Error: %s\n", lineNo, columnNo,
            "getBoolVal called on non-BOOL_LIT token");
    exit(2);
  }
  return (lexeme == "true");
}

static TOKEN returnTok(std::string lexVal, int tok_type) {
  TOKEN return_tok;
  return_tok.lexeme = lexVal;
  return_tok.type = tok_type;
  return_tok.lineNo = lineNo;
  return_tok.columnNo = columnNo - lexVal.length() - 1;
  return return_tok;
}

/// LogLexicalError - report an invalid character and stop
static TOKEN LogLexicalError(TOKEN tok, const std::string &msg) {
  fprintf(stderr, "%d:%d Lexical error: %s\n", tok.lineNo, tok.columnNo, msg.c_str());
  exit(2);
  return tok;
}

// Read file line by line -- or look for \n and if found add 1 to line number
// and reset column number to 0
/// gettok - Return the next token from standard input.
static TOKEN gettok() {

  static int LastChar = ' ';
  static int NextChar = ' ';

  // Skip any whitespace
  while (isspace(LastChar)) {
    // only counting '\n' so that the Windows "\r\n" endings count as one line
    if (LastChar == '\n') {
      lineNo++;
      columnNo = 1;
    }
    LastChar = getc(pFile);
    columnNo++;
  }

  if (isalpha(LastChar) ||
      (LastChar == '_')) { // identifier: [a-zA-Z_][a-zA-Z_0-9]*
    globalLexeme = LastChar;
    columnNo++;

    while (isalnum((LastChar = getc(pFile))) || (LastChar == '_')) {
      globalLexeme += LastChar;
      columnNo++;
    }

    if (globalLexeme == "int")
      return returnTok("int", INT_TOK);
    if (globalLexeme == "bool")
      return returnTok("bool", BOOL_TOK);
    if (globalLexeme == "float")
      return returnTok("float", FLOAT_TOK);
    if (globalLexeme == "void")
      return returnTok("void", VOID_TOK);
    if (globalLexeme == "bool")
      return returnTok("bool", BOOL_TOK);
    if (globalLexeme == "extern")
      return returnTok("extern", EXTERN);
    if (globalLexeme == "if")
      return returnTok("if", IF);
    if (globalLexeme == "else")
      return returnTok("else", ELSE);
    if (globalLexeme == "while")
      return returnTok("while", WHILE);
    if (globalLexeme == "return")
      return returnTok("return", RETURN);
    if (globalLexeme == "true") {
      //   BoolVal = true;
      return returnTok("true", BOOL_LIT);
    }
    if (globalLexeme == "false") {
      //   BoolVal = false;
      return returnTok("false", BOOL_LIT);
    }
    return returnTok(globalLexeme.c_str(), IDENT);
  }

  if (LastChar == '=') {
    NextChar = getc(pFile);
    if (NextChar == '=') { // EQ: ==
      LastChar = getc(pFile);
      columnNo += 2;
      return returnTok("==", EQ);
    } else {
      LastChar = NextChar;
      columnNo++;
      return returnTok("=", ASSIGN);
    }
  }

  if (LastChar == '{') {
    LastChar = getc(pFile);
    columnNo++;
    return returnTok("{", LBRA);
  }
  if (LastChar == '}') {
    LastChar = getc(pFile);
    columnNo++;
    return returnTok("}", RBRA);
  }
  if (LastChar == '(') {
    LastChar = getc(pFile);
    columnNo++;
    return returnTok("(", LPAR);
  }
  if (LastChar == ')') {
    LastChar = getc(pFile);
    columnNo++;
    return returnTok(")", RPAR);
  }
  if (LastChar == ';') {
    LastChar = getc(pFile);
    columnNo++;
    return returnTok(";", SC);
  }
  if (LastChar == ',') {
    LastChar = getc(pFile);
    columnNo++;
    return returnTok(",", COMMA);
  }

  if (isdigit(LastChar) || LastChar == '.') { 
    std::string NumStr;

    if (LastChar == '.') {
      do {
        NumStr += LastChar;
        LastChar = getc(pFile);
        columnNo++;
      } while (isdigit(LastChar));
      if (NumStr == ".")
        return LogLexicalError(returnTok(NumStr, INVALID), "invalid character '.'");
      return LogLexicalError(returnTok(NumStr, INVALID), "improperly written float '" + NumStr + "', a digit is needed before '.'");
    } else {
      do { 
        NumStr += LastChar;
        LastChar = getc(pFile);
        columnNo++;
      } while (isdigit(LastChar));

      if (LastChar == '.') { 
        do {
          NumStr += LastChar;
          LastChar = getc(pFile);
          columnNo++;
        } while (isdigit(LastChar));

        if (NumStr.back() == '.')
          return LogLexicalError(returnTok(NumStr, INVALID), "malformed float '" + NumStr + "', a digit is needed after '.'");
        return returnTok(NumStr, FLOAT_LIT);
      } else { 
        if (NumStr.length() > 10 || std::stoll(NumStr) > 2147483647)
          return LogLexicalError(returnTok(NumStr, INVALID), "integer literal '" + NumStr + "' is too large for int");
        return returnTok(NumStr, INT_LIT);
      }
    }
  }

  if (LastChar == '&') {
    NextChar = getc(pFile);
    if (NextChar == '&') { 
      LastChar = getc(pFile);
      columnNo += 2;
      return returnTok("&&", AND);
    } else {
      LastChar = NextChar;
      columnNo++;
      return LogLexicalError(returnTok("&", INVALID), "invalid character '&', did you mean '&&'?");
    }
  }

  if (LastChar == '|') {
    NextChar = getc(pFile);
    if (NextChar == '|') { // OR: ||
      LastChar = getc(pFile);
      columnNo += 2;
      return returnTok("||", OR);
    } else {
      LastChar = NextChar;
      columnNo++;
      // a single '|' is not a MiniC token
      return LogLexicalError(returnTok("|", INVALID), "invalid character '|', did you mean '||'?");
    }
  }

  if (LastChar == '!') {
    NextChar = getc(pFile);
    if (NextChar == '=') { // NE: !=
      LastChar = getc(pFile);
      columnNo += 2;
      return returnTok("!=", NE);
    } else {
      LastChar = NextChar;
      columnNo++;
      return returnTok("!", NOT);
      ;
    }
  }

  if (LastChar == '<') {
    NextChar = getc(pFile);
    if (NextChar == '=') { // LE: <=
      LastChar = getc(pFile);
      columnNo += 2;
      return returnTok("<=", LE);
    } else {
      LastChar = NextChar;
      columnNo++;
      return returnTok("<", LT);
    }
  }

  if (LastChar == '>') {
    NextChar = getc(pFile);
    if (NextChar == '=') { // GE: >=
      LastChar = getc(pFile);
      columnNo += 2;
      return returnTok(">=", GE);
    } else {
      LastChar = NextChar;
      columnNo++;
      return returnTok(">", GT);
    }
  }

  if (LastChar == '/') { // could be division or could be the start of a comment
    LastChar = getc(pFile);
    columnNo++;
    if (LastChar == '/') { // definitely a comment
      do {
        LastChar = getc(pFile);
        columnNo++;
      } while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');

      if (LastChar != EOF)
        return gettok();
    } else
      return returnTok("/", DIV);
  }

  // Check for end of file.  Don't eat the EOF.
  if (LastChar == EOF) {
    columnNo++;
    return returnTok("0", EOF_TOK);
  }

  // Otherwise, just return the character as its ascii value.
  int ThisChar = LastChar;
  std::string s(1, ThisChar);
  LastChar = getc(pFile);
  columnNo++;
  // the only single characters left that are MiniC tokens
  if (std::string("+-*%[]").find((char)ThisChar) == std::string::npos)
    return LogLexicalError(returnTok(s, INVALID), "invalid character '" + s + "'");
  return returnTok(s, int(ThisChar));
}

//===----------------------------------------------------------------------===//
// Parser
//===----------------------------------------------------------------------===//

/// CurTok/getNextToken - Provide a simple token buffer.  CurTok is the current
/// token the parser is looking at.  getNextToken reads another token from the
/// lexer and updates CurTok with its results.
static TOKEN CurTok;
static std::deque<TOKEN> tok_buffer;

static TOKEN getNextToken() {

  if (tok_buffer.size() == 0)
    tok_buffer.push_back(gettok());

  TOKEN temp = tok_buffer.front();
  tok_buffer.pop_front();

  return CurTok = temp;
}

static void putBackToken(TOKEN tok) { tok_buffer.push_front(tok); }

/// indent - leading spaces for a node printed at the given tree depth
static std::string astIndent(int depth) { return std::string(depth * 2, ' '); }

// ASTnode - the base class for all AST nodes
class ASTnode {

public:
  virtual ~ASTnode() {}
  virtual Value *codegen() { return nullptr; };
  // returns this node and its children as an indented tree - one node per line
  virtual std::string to_string(int depth = 0) const { return ""; };
  // only the vars and array elements can be on the left of =
  virtual bool isAssignable() const { return false; }
};

// a class for integer literals
class IntASTnode : public ASTnode {
  int Val;
  TOKEN Tok;
  std::string Name;

public:
  IntASTnode(TOKEN tok, int val) : Val(val), Tok(tok) {}
  const std::string &getType() const { return Tok.lexeme; }
  Value *codegen() override;
  std::string to_string(int depth = 0) const override {
    return astIndent(depth) + "IntLiteral " + std::to_string(Val) + "\n";
  }
};

/// class for bools
class BoolASTnode : public ASTnode {
  bool Bool;
  TOKEN Tok;

public:
  BoolASTnode(TOKEN tok, bool B) : Bool(B), Tok(tok) {}
  const std::string &getType() const { return Tok.lexeme; }
  Value *codegen() override;
  std::string to_string(int depth = 0) const override {
    return astIndent(depth) + "BoolLiteral " + (Bool ? "true" : "false") + "\n";
  }
};

/// The node class for floating point literals eg "1.0"
class FloatASTnode : public ASTnode {
  double Val;
  TOKEN Tok;

public:
  FloatASTnode(TOKEN tok, double Val) : Val(Val), Tok(tok) {}
  const std::string &getType() const { return Tok.lexeme; }
  Value *codegen() override;
  // print the source spelling so 3.14 doesn't show as 3.140000
  std::string to_string(int depth = 0) const override {
    return astIndent(depth) + "FloatLiteral " + Tok.lexeme + "\n";
  }
};

// the class for referencing a variable (i.e. identifier)
enum IDENT_TYPE { IDENTIFIER = 0 };
class VariableASTnode : public ASTnode {
protected:
  TOKEN Tok;
  std::string Name;
  IDENT_TYPE VarType;

public:
  VariableASTnode(TOKEN tok, const std::string &Name)
      : Tok(tok), Name(Name), VarType(IDENT_TYPE::IDENTIFIER) {}
  const std::string &getName() const { return Name; }
  const std::string &getType() const { return Tok.lexeme; }
  const IDENT_TYPE getVarType() const { return VarType; }
  // getter for token
  const TOKEN &getTok() const { return Tok; }
  Value *codegen() override;
  std::string to_string(int depth = 0) const override {
    bool isAssignable() const override {return true;}
    return astIndent(depth) + "Variable " + Name + "\n";
  }
};

/// ParamAST - Class for a parameter declaration
class ParamAST {
  std::string Name;
  std::string Type;

public:
  ParamAST(const std::string &name, const std::string &type)
      : Name(name), Type(type) {}
  const std::string &getName() const { return Name; }
  const std::string &getType() const { return Type; }
  std::string to_string(int depth) const {
    return astIndent(depth) + "Param " + Name + " : " + Type + "\n";
  }
};

/// DeclAST - Base class for declarations, variables and functions
class DeclAST : public ASTnode {

public:
  virtual ~DeclAST() {}
};

/// VarDeclAST - Class for a variable declaration
class VarDeclAST : public DeclAST {
  std::unique_ptr<VariableASTnode> Var;
  std::string Type;

public:
  VarDeclAST(std::unique_ptr<VariableASTnode> var, const std::string &type)
      : Var(std::move(var)), Type(type) {}
  const std::string &getType() const { return Type; }
  const std::string &getName() const { return Var->getName(); }
  Value *codegen() override;
  std::string to_string(int depth = 0) const override {
    return astIndent(depth) + "LocalVarDecl " + Var->getName() + " : " + Type + "\n";
  }
};

/// GlobVarDeclAST - Class for a Global variable declaration
class GlobVarDeclAST : public DeclAST {
  std::unique_ptr<VariableASTnode> Var;
  std::string Type;

public:
  GlobVarDeclAST(std::unique_ptr<VariableASTnode> var, const std::string &type)
      : Var(std::move(var)), Type(type) {}
  const std::string &getType() const { return Type; }
  const std::string &getName() const { return Var->getName(); }
  Value *codegen() override;
  std::string to_string(int depth = 0) const override {
    return astIndent(depth) + "GlobalVarDecl " + Var->getName() + " : " + Type + "\n";
  }
};

/// FunctionPrototypeAST - Class for a function declaration's signature
class FunctionPrototypeAST {
  // name token for error positions
  TOKEN Tok;
  std::string Name;
  std::string Type;
  std::vector<std::unique_ptr<ParamAST>> Params; // vector of parameters

public:
  FunctionPrototypeAST(TOKEN tok, const std::string &name, const std::string &type,
                       std::vector<std::unique_ptr<ParamAST>> params)
      : Tok(std::move(tok)), Name(name), Type(type), Params(std::move(params)) {}

  const std::string &getName() const { return Name; }
  const std::string &getType() const { return Type; }
  const TOKEN &getTok() const { return Tok; }
  int getSize() const { return Params.size(); }
  std::vector<std::unique_ptr<ParamAST>> &getParams() { return Params; }
  Function *codegen();
  // kind labels the line, e.g. "ExternFunction" or "FunctionPrototype"
  std::string to_string(int depth, const std::string &kind = "FunctionPrototype") const {
    std::string s = astIndent(depth) + kind + " " + Name + " : " + Type + "\n";
    for (const auto &P : Params)
      s += P->to_string(depth + 1);
    return s;
  }
};

/// Binary expression, e.g. a + b, a && b
class BinaryExprAST : public ASTnode {
  std::unique_ptr<ASTnode> Left;
  TOKEN OpTok;
  std::unique_ptr<ASTnode> Right;

public:
  BinaryExprAST(std::unique_ptr<ASTnode> left, TOKEN opTok,
                std::unique_ptr<ASTnode> right)
      : Left(std::move(left)), OpTok(std::move(opTok)),
        Right(std::move(right)) {}
  Value *codegen() override;
  std::string to_string(int depth = 0) const override {
    return astIndent(depth) + "BinaryExpr '" + OpTok.lexeme + "'\n" + Left->to_string(depth + 1) + Right->to_string(depth + 1);
  }
};

/// Unary expression, eg !x
class UnaryExprAST : public ASTnode {
  TOKEN OpTok;
  std::unique_ptr<ASTnode> Operand;

public:
  UnaryExprAST(TOKEN opTok, std::unique_ptr<ASTnode> operand)
      : OpTok(std::move(opTok)), Operand(std::move(operand)) {}
  Value *codegen() override;
  std::string to_string(int depth = 0) const override {
    return astIndent(depth) + "UnaryExpr '" + OpTok.lexeme + "'\n" + Operand->to_string(depth + 1);
  }
};

/// Assignment
class AssignExprAST : public ASTnode {
  std::unique_ptr<VariableASTnode> Var;
  std::unique_ptr<ASTnode> RHS;

public:
  AssignExprAST(std::unique_ptr<VariableASTnode> var,
                std::unique_ptr<ASTnode> rhs)
      : Var(std::move(var)), RHS(std::move(rhs)) {}
  Value *codegen() override;
  std::string to_string(int depth = 0) const override {
    return astIndent(depth) + "Assign\n" + Var->to_string(depth + 1) + RHS->to_string(depth + 1);
  }
};

/// ProgramAST - Root of the AST: externs followed by declarations
class ProgramAST : public ASTnode {
  std::vector<std::unique_ptr<FunctionPrototypeAST>> Externs;
  std::vector<std::unique_ptr<ASTnode>> Decls;

public:
  ProgramAST(std::vector<std::unique_ptr<FunctionPrototypeAST>> externs,
             std::vector<std::unique_ptr<ASTnode>> decls)
      : Externs(std::move(externs)), Decls(std::move(decls)) {}
  Value *codegen() override;
  std::string to_string(int depth = 0) const override {
    std::string s = astIndent(depth) + "Program\n";
    for (const auto &E : Externs)
      s += E->to_string(depth + 1, "ExternFunction");
    for (const auto &D : Decls)
      s += D->to_string(depth + 1);
    return s;
  }
};

/// BlockAST - Class for a block with declarations followed by statements
class BlockAST : public ASTnode {
  std::vector<std::unique_ptr<VarDeclAST>> LocalDecls; // vector of local decls
  std::vector<std::unique_ptr<ASTnode>> Stmts;         // vector of statements

public:
  BlockAST(std::vector<std::unique_ptr<VarDeclAST>> localDecls,
           std::vector<std::unique_ptr<ASTnode>> stmts)
      : LocalDecls(std::move(localDecls)), Stmts(std::move(stmts)) {}
  Value *codegen() override;
  std::string to_string(int depth = 0) const override {
    std::string s = astIndent(depth) + "Block\n";
    for (const auto &D : LocalDecls)
      s += D->to_string(depth + 1);
    for (const auto &S : Stmts)
      s += S->to_string(depth + 1);
    return s;
  }
};

/// FunctionDeclAST - This class represents a function definition itself.
class FunctionDeclAST : public DeclAST {
  std::unique_ptr<FunctionPrototypeAST> Proto;
  std::unique_ptr<ASTnode> Block;

public:
  FunctionDeclAST(std::unique_ptr<FunctionPrototypeAST> Proto,
                  std::unique_ptr<ASTnode> Block)
      : Proto(std::move(Proto)), Block(std::move(Block)) {}
  Value *codegen() override;
  std::string to_string(int depth = 0) const override {
    return astIndent(depth) + "FunctionDecl\n" + Proto->to_string(depth + 1) + Block->to_string(depth + 1);
  }
};

/// IfExprAST - Expression class for if/then/else.
class IfExprAST : public ASTnode {
  std::unique_ptr<ASTnode> Cond, Then, Else;

public:
  IfExprAST(std::unique_ptr<ASTnode> Cond, std::unique_ptr<ASTnode> Then,
            std::unique_ptr<ASTnode> Else)
      : Cond(std::move(Cond)), Then(std::move(Then)), Else(std::move(Else)) {}
  Value *codegen() override;
  std::string to_string(int depth = 0) const override {
    std::string s = astIndent(depth) + "If\n";
    s += astIndent(depth + 1) + "Condition\n" + Cond->to_string(depth + 2);
    s += astIndent(depth + 1) + "Then\n" + Then->to_string(depth + 2);
    // else branch is optional
    if (Else)
      s += astIndent(depth + 1) + "Else\n" + Else->to_string(depth + 2);
    return s;
  }
};

/// WhileExprAST - Expression class for while.
class WhileExprAST : public ASTnode {
  std::unique_ptr<ASTnode> Cond, Body;

public:
  WhileExprAST(std::unique_ptr<ASTnode> cond, std::unique_ptr<ASTnode> body)
      : Cond(std::move(cond)), Body(std::move(body)) {}
  Value *codegen() override;
  std::string to_string(int depth = 0) const override {
    return astIndent(depth) + "While\n" + astIndent(depth + 1) + "Condition\n" +
           Cond->to_string(depth + 2) + astIndent(depth + 1) + "Body\n" + Body->to_string(depth + 2);
  }
};

/// ReturnAST - Class for a return value
class ReturnAST : public ASTnode {
  // return token for error positions
  TOKEN RetTok;
  std::unique_ptr<ASTnode> Val;

public:
  ReturnAST(TOKEN retTok, std::unique_ptr<ASTnode> value)
      : RetTok(std::move(retTok)), Val(std::move(value)) {}
  Value *codegen() override;
  // return value is optional: "return;" has none
  std::string to_string(int depth = 0) const override {
    return astIndent(depth) + "Return\n" + (Val ? Val->to_string(depth + 1) : "");
  }
};

/// ArgsAST - Class for a function call and its arguments
class ArgsAST : public ASTnode {
  // callee token for error positions
  TOKEN Tok;
  std::string Callee;
  std::vector<std::unique_ptr<ASTnode>> ArgsList;

public:
  ArgsAST(TOKEN tok, const std::string &Callee,
          std::vector<std::unique_ptr<ASTnode>> list)
      : Tok(std::move(tok)), Callee(Callee), ArgsList(std::move(list)) {}
  Value *codegen() override;
  std::string to_string(int depth = 0) const override {
    std::string s = astIndent(depth) + "Call " + Callee + "\n";
    for (const auto &A : ArgsList)
      s += A->to_string(depth + 1);
    return s;
  }
};

/// lets an AST node be printed with llvm::outs() << node
llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const ASTnode &ast) {
  os << ast.to_string();
  return os;
}


/// LogError* - These are little helper function for error handling.
std::unique_ptr<ASTnode> LogError(TOKEN tok, const char *Str) {
  fprintf(stderr, "%d:%d Syntax error: %s\n", tok.lineNo, tok.columnNo, Str);
  exit(2);
  return nullptr;
}

std::unique_ptr<FunctionPrototypeAST> LogErrorP(TOKEN tok, const char *Str) {
  LogError(tok, Str);
  exit(2);
  return nullptr;
}

std::unique_ptr<ASTnode> LogError(const char *Str) {
  fprintf(stderr, "Syntax error: %s\n", Str);
  exit(2);
  return nullptr;
}

//===----------------------------------------------------------------------===//
// Recursive Descent - Function call for each production
//===----------------------------------------------------------------------===//

static std::unique_ptr<ASTnode> ParseDecl();
static std::unique_ptr<ASTnode> ParseStmt();
static std::unique_ptr<ASTnode> ParseBlock();
static std::unique_ptr<ASTnode> ParseExper();
static std::unique_ptr<ParamAST> ParseParam();
static std::unique_ptr<VarDeclAST> ParseLocalDecl();
static std::vector<std::unique_ptr<ASTnode>> ParseStmtListPrime();

/// isFirstOfExpr - this returns true if token type t is in FIRST(expr)
static bool isFirstOfExpr(int t) {
  return t == IDENT || t == INT_LIT || t == FLOAT_LIT || t == BOOL_LIT || t == LPAR || t == MINUS || t == NOT;
}

/// isFirstOfStmt - returns true if token type t is in FIRST(stmt)
static bool isFirstOfStmt(int t) {
  return isFirstOfExpr(t) || t == SC || t == LBRA || t == IF || t == WHILE || t == RETURN;
}

// element ::= FLOAT_LIT
static std::unique_ptr<ASTnode> ParseFloatNumberExpr() {
  auto Result = std::make_unique<FloatASTnode>(CurTok, CurTok.getFloatVal());
  getNextToken(); // consume the number
  return std::move(Result);
}

// element ::= INT_LIT
static std::unique_ptr<ASTnode> ParseIntNumberExpr() {
  auto Result = std::make_unique<IntASTnode>(CurTok, CurTok.getIntVal());
  getNextToken(); // consume the number
  return std::move(Result);
}

// element ::= BOOL_LIT
static std::unique_ptr<ASTnode> ParseBoolExpr() {
  auto Result = std::make_unique<BoolASTnode>(CurTok, CurTok.getBoolVal());
  getNextToken(); // consume the number
  return std::move(Result);
}

// param_list_prime ::= "," param param_list_prime|eplison
static std::vector<std::unique_ptr<ParamAST>> ParseParamListPrime() {
  std::vector<std::unique_ptr<ParamAST>> param_list;

  if (CurTok.type == COMMA) { // more parameters in list
    getNextToken();           // eat ","

    auto param = ParseParam();
    if (param) {
      param_list.push_back(std::move(param));
      auto param_list_prime = ParseParamListPrime();
      for (unsigned i = 0; i < param_list_prime.size(); i++) {
        param_list.push_back(std::move(param_list_prime.at(i)));
      }
    }
  } else if (CurTok.type == RPAR) { // FOLLOW(param_list_prime)
    // expand by param_list_prime ::= ε
    // do nothing
  } else {
    LogError(CurTok, "expected ',' or ')' in list of parameter declarations");
  }

  return param_list;
}

// param ::= var_type IDENT
static std::unique_ptr<ParamAST> ParseParam() {
  if (CurTok.type != INT_TOK && CurTok.type != FLOAT_TOK &&
      CurTok.type != BOOL_TOK) {
    LogError(CurTok, "expected 'int', 'float' or 'bool' in parameter declaration");
    return nullptr;
  }
  std::string Type = CurTok.lexeme;
  getNextToken(); // eat type

  if (CurTok.type != IDENT) {
    LogError(CurTok, "expected parameter name after type");
    return nullptr;
  }
  std::string Name = CurTok.getIdentifierStr();
  getNextToken(); // eat IDENT

  return std::make_unique<ParamAST>(Name, Type);
}

// param_list ::= param param_list_prime
static std::vector<std::unique_ptr<ParamAST>> ParseParamList() {
  std::vector<std::unique_ptr<ParamAST>> param_list;

  auto param = ParseParam();
  if (param) {
    param_list.push_back(std::move(param));
    auto param_list_prime = ParseParamListPrime();
    for (unsigned i = 0; i < param_list_prime.size(); i++) {
      param_list.push_back(std::move(param_list_prime.at(i)));
    }
  }

  return param_list;
}

// params ::= param_list |"void" | epsilon
static std::vector<std::unique_ptr<ParamAST>> ParseParams() {
  std::vector<std::unique_ptr<ParamAST>> param_list;

  if (CurTok.type == INT_TOK || CurTok.type == FLOAT_TOK || CurTok.type == BOOL_TOK) { // FIRST(param_list)
    auto list = ParseParamList();
    for (unsigned i = 0; i < list.size(); i++) {
      param_list.push_back(std::move(list.at(i)));
    }
  } else if (CurTok.type == VOID_TOK) { // FIRST("void")
    getNextToken(); // eat 'void'
    if (CurTok.type != RPAR) {
      LogError(CurTok, "expected ')' after 'void' in parameter list");
    }
  } else if (CurTok.type == RPAR) { // FOLLOW(params)
    // expand by params ::= ε
    // do nothing
  } else {
    LogError(CurTok, "expected 'int', 'float', 'bool', 'void' or ')' in parameter list");
  }

  return param_list;
}

// args ::= arg_list | epsilon
// arg_list ::= expr arg_list'
// arg_list' ::= "," expr arg_list' | epsilon
static std::vector<std::unique_ptr<ASTnode>> ParseArgs() {
  std::vector<std::unique_ptr<ASTnode>> args;
  if (CurTok.type == RPAR) // FOLLOW(args): empty argument list
    return args;

  args.push_back(ParseExper());
  while (CurTok.type == COMMA) { // arg_list'
    // eat ','
    getNextToken();
    args.push_back(ParseExper());
  }
  if (CurTok.type != RPAR)
    LogError(CurTok, "expected ',' or ')' in the function call args");
  return args;
}

// primary ::= "(" expr ")" | IDENT identifier_tail| INT_LIT | FLOAT_LIT | BOOL_LIT
// identifier_tail ::= "(" args ")" | epsilon
static std::unique_ptr<ASTnode> ParsePrimary() {
  switch (CurTok.type) {
  case INT_LIT:
    return ParseIntNumberExpr();
  case FLOAT_LIT:
    return ParseFloatNumberExpr();
  case BOOL_LIT:
    return ParseBoolExpr();
  case LPAR: {
    getNextToken(); // eat '('
    auto E = ParseExper();
    if (CurTok.type != RPAR)
      return LogError(CurTok, "expected ')' to close parenthesised expression");
    getNextToken(); // eat ')'
    return E;
  }
  case IDENT: {
    TOKEN IdTok = CurTok;
    getNextToken(); // eat IDENT
    if (CurTok.type == LPAR) { // identifier_tail ::= "(" args ")"
      // eat '('
      getNextToken();          
      auto Args = ParseArgs();
       // eat ')'
      getNextToken();
      return std::make_unique<ArgsAST>(IdTok, IdTok.lexeme, std::move(Args));
    }
    // identifier_tail ::= ε
    return std::make_unique<VariableASTnode>(IdTok, IdTok.lexeme);
  }
  default:
    return LogError(CurTok, "expected an expression - an identifier, literal, '(', '-' or '!'");
  }
}

// unary ::= "-" unary | "!" unary | primary
static std::unique_ptr<ASTnode> ParseUnary() {
  if (CurTok.type == MINUS || CurTok.type == NOT) {
    TOKEN OpTok = CurTok;
    getNextToken(); // eat '-' or '!'
    auto Operand = ParseUnary();
    return std::make_unique<UnaryExprAST>(OpTok, std::move(Operand));
  }
  return ParsePrimary();
}

// mul_expr ::= unary mul_expr'
// mul_expr' ::= ("*" | "/" | "%") unary mul_expr' | epsilon
static std::unique_ptr<ASTnode> ParseMulExpr() {
  auto LHS = ParseUnary();
  while (CurTok.type == ASTERIX || CurTok.type == DIV || CurTok.type == MOD) {
    TOKEN OpTok = CurTok;
    // eat operator
    getNextToken(); 
    auto RHS = ParseUnary();
    LHS = std::make_unique<BinaryExprAST>(std::move(LHS), OpTok, std::move(RHS));
  }
  return LHS;
}

// add_expr ::= mul_expr add_expr'
// add_expr' ::= ("+" | "-") mul_expr add_expr' | ε
static std::unique_ptr<ASTnode> ParseAddExpr() {
  auto LHS = ParseMulExpr();
  while (CurTok.type == PLUS || CurTok.type == MINUS) {
    TOKEN OpTok = CurTok;
    // eat operator
    getNextToken(); 
    auto RHS = ParseMulExpr();
    LHS = std::make_unique<BinaryExprAST>(std::move(LHS), OpTok, std::move(RHS));
  }
  return LHS;
}

// relational_expr ::= add_expr relational_expr'
// relational_expr' ::= ("<=" | "<" | ">=" | ">") add_expr relational_expr' | epsilon
static std::unique_ptr<ASTnode> ParseRelationalExpr() {
  auto LHS = ParseAddExpr();
  while (CurTok.type == LE || CurTok.type == LT || CurTok.type == GE ||
         CurTok.type == GT) {
    TOKEN OpTok = CurTok;
    // eat operator
    getNextToken(); 
    auto RHS = ParseAddExpr();
    LHS = std::make_unique<BinaryExprAST>(std::move(LHS), OpTok, std::move(RHS));
  }
  return LHS;
}

// eq_expr ::= relational_expr eq_expr'
// eq_expr' ::= ("==" | "!=") relational_expr eq_expr' | epsilon
static std::unique_ptr<ASTnode> ParseEqExpr() {
  auto LHS = ParseRelationalExpr();
  while (CurTok.type == EQ || CurTok.type == NE) {
    TOKEN OpTok = CurTok;
    // eat operator
    getNextToken(); 
    auto RHS = ParseRelationalExpr();
    LHS = std::make_unique<BinaryExprAST>(std::move(LHS), OpTok, std::move(RHS));
  }
  return LHS;
}

// and_expr ::= eq_expr and_expr'
// and_expr' ::= "&&" eq_expr and_expr' | epsilon
static std::unique_ptr<ASTnode> ParseAndExpr() {
  auto LHS = ParseEqExpr();
  while (CurTok.type == AND) {
    TOKEN OpTok = CurTok;
    // eat '&&'
    getNextToken(); 
    auto RHS = ParseEqExpr();
    LHS = std::make_unique<BinaryExprAST>(std::move(LHS), OpTok, std::move(RHS));
  }
  return LHS;
}

// rval ::= and_expr rval'
// rval' ::= "||" and_expr rval' | epsilon
static std::unique_ptr<ASTnode> ParseRval() {
  auto LHS = ParseAndExpr();
  while (CurTok.type == OR) {
    TOKEN OpTok = CurTok;
    // eat '||'
    getNextToken(); 
    auto RHS = ParseAndExpr();
    LHS = std::make_unique<BinaryExprAST>(std::move(LHS), OpTok, std::move(RHS));
  }
  return LHS;
}

// expr ::= rval expr_tail
// expr_tail ::= "=" expr | epsilon
// the left of = must be a variable or an arr element
static std::unique_ptr<ASTnode> ParseExper() {
  auto LHS = ParseRval();
  if (CurTok.type != ASSIGN)
    return LHS;
  if (!LHS->isAssignable())
    return LogError(CurTok, "left side of the '=' must be a variable or an array element");
  // consume '='
  getNextToken();
  // right-recursive, so a = b = c is right-associative
  auto RHS = ParseExper();
  std::unique_ptr<VariableASTnode> Var(static_cast<VariableASTnode *>(LHS.release()));
  return std::make_unique<AssignExprAST>(std::move(Var), std::move(RHS));
}

// expr_stmt ::= expr ";"|  ";"
static std::unique_ptr<ASTnode> ParseExperStmt() {

  if (CurTok.type == SC) { // empty statement
    getNextToken();   
    return nullptr;
  } else {
    auto expr = ParseExper();
    if (expr) {
      if (CurTok.type == SC) {
        getNextToken(); 
        return expr;
      } else {
        LogError(CurTok, "expected ';' to end expression statement");
      }
    } else
      return nullptr;
  }
  return nullptr;
}

// else_stmt ::= "else" block | epsilon
static std::unique_ptr<ASTnode> ParseElseStmt() {
  // FIRST(else_stmt)
  if (CurTok.type == ELSE) {
    // eat 'else'
    getNextToken();
    if (CurTok.type != LBRA)
      return LogError(CurTok, "expected '{' to start else block");
    return ParseBlock();
  } else if (isFirstOfStmt(CurTok.type) || CurTok.type == RBRA) {
    // FOLLOW(else_stmt): expand by else_stmt ::= epsilon
    return nullptr;
  }
  return LogError(CurTok, "expected 'else', a statement or '}' after if block");
}

// if_stmt ::= "if" "(" expr ")" block else_stmt
static std::unique_ptr<ASTnode> ParseIfStmt() {
  getNextToken(); // eat the if.
  if (CurTok.type == LPAR) {
    getNextToken(); // eat (
    // condition.
    auto Cond = ParseExper();
    if (!Cond)
      return nullptr;
    if (CurTok.type != RPAR)
      return LogError(CurTok, "expected ')' after if condition");
    getNextToken(); // eat )

    if (!(CurTok.type == LBRA)) {
      return LogError(CurTok, "expected '{' to start the if block");
    }

    auto Then = ParseBlock();
    if (!Then)
      return nullptr;
    auto Else = ParseElseStmt();
    return std::make_unique<IfExprAST>(std::move(Cond), std::move(Then), std::move(Else));

  } else
    return LogError(CurTok, "expected '(' after 'if'");

  return nullptr;
}

// return_stmt ::= "return" return_tail
// return_tail ::= ";" | expr ";"
static std::unique_ptr<ASTnode> ParseReturnStmt() {
  // keep the return token for error positions
  TOKEN RetTok = CurTok;
  // eat 'return'
  getNextToken();

  if (CurTok.type == SC) {
    // eat ';'
    getNextToken();
    // return with no value
    return std::make_unique<ReturnAST>(RetTok, nullptr);
  }
  // FIRST(expr)
  if (isFirstOfExpr(CurTok.type)) {
    auto Val = ParseExper();
    if (CurTok.type != SC)
      return LogError(CurTok, "expected a ';' after return value");
    // eat ';'
    getNextToken();
    return std::make_unique<ReturnAST>(RetTok, std::move(Val));
  }
  return LogError(CurTok, "expected a ';' or an expression after 'return'");
}

// while_stmt ::= "while" "(" expr ")" stmt
static std::unique_ptr<ASTnode> ParseWhileStmt() {

  getNextToken(); // eat the while.
  if (CurTok.type == LPAR) {
    getNextToken(); // eat (
    // condition.
    auto Cond = ParseExper();
    if (!Cond)
      return nullptr;
    if (CurTok.type != RPAR)
      return LogError(CurTok, "expected ')' after while condition");
    getNextToken(); // eat )

    auto Body = ParseStmt();
    // an empty statement ';' as the body becomes an empty block
    if (!Body)
      Body = std::make_unique<BlockAST>(std::vector<std::unique_ptr<VarDeclAST>>(),std::vector<std::unique_ptr<ASTnode>>());

    return std::make_unique<WhileExprAST>(std::move(Cond), std::move(Body));
  } else
    return LogError(CurTok, "expected '(' after 'while'");
}

// stmt ::= expr_stmt|  block| if_stmt| while_stmt| return_stmt
static std::unique_ptr<ASTnode> ParseStmt() {

    // FIRST(expr_stmt) = FIRST(expr) plus ';'
  if (isFirstOfExpr(CurTok.type) || CurTok.type == SC) {
    // expand by stmt ::= expr_stmt
    auto expr_stmt = ParseExperStmt();
    return expr_stmt;
  } else if (CurTok.type == LBRA) { // FIRST(block)
    auto block_stmt = ParseBlock();
    if (block_stmt) {
      return block_stmt;
    }
  } else if (CurTok.type == IF) { // FIRST(if_stmt)
    auto if_stmt = ParseIfStmt();
    if (if_stmt) {
      return if_stmt;
    }
  } else if (CurTok.type == WHILE) { // FIRST(while_stmt)
    auto while_stmt = ParseWhileStmt();
    if (while_stmt) {
      return while_stmt;
    }
  } else if (CurTok.type == RETURN) { // FIRST(return_stmt)
    auto return_stmt = ParseReturnStmt();
    if (return_stmt) {
      return return_stmt;
    }
  }
  // else if(CurTok.type == RBRA) { // FOLLOW(stmt_list_prime)
  //  expand by stmt_list_prime ::= epsilon
  //  do nothing
  //}
  else { // syntax error
    return LogError(CurTok, "expected a statement: expression, ';', '{', 'if', 'while' or 'return'");
  }
  return nullptr;
}

// stmt_list ::= stmt stmt_list | epsilon
static std::vector<std::unique_ptr<ASTnode>> ParseStmtList() {
  return ParseStmtListPrime();
}

// stmt_list_prime ::= stmt stmt_list_prime
//                  |  ε
static std::vector<std::unique_ptr<ASTnode>> ParseStmtListPrime() {
  // vector of statements
  std::vector<std::unique_ptr<ASTnode>> stmt_list;

  // FIRST(stmt)- expand by stmt_list ::= stmt stmt_list
  if (isFirstOfStmt(CurTok.type)) {
    auto stmt = ParseStmt();
    if (stmt) {
      stmt_list.push_back(std::move(stmt));
    }
    auto stmt_prime = ParseStmtListPrime();
    for (unsigned i = 0; i < stmt_prime.size(); i++) {
      stmt_list.push_back(std::move(stmt_prime.at(i)));
    }
  } else if (CurTok.type == RBRA) {
    // FOLLOW(stmt_list): expand by stmt_list ::= epsilon
  } else {
    LogError(CurTok, "expected a statement or '}'");
  }

  // stmt_list can be empty, e.g. an empty block
  return stmt_list;
}

// local_decls_prime ::= local_decl local_decls_prime
//                    |  ε
static std::vector<std::unique_ptr<VarDeclAST>> ParseLocalDeclsPrime() {
  std::vector<std::unique_ptr<VarDeclAST>>
      local_decls_prime; // vector of local decls

  if (CurTok.type == INT_TOK || CurTok.type == FLOAT_TOK ||
      CurTok.type == BOOL_TOK) { // FIRST(local_decl)
    auto local_decl = ParseLocalDecl();
    if (local_decl) {
      local_decls_prime.push_back(std::move(local_decl));
    }
    auto prime = ParseLocalDeclsPrime();
    for (unsigned i = 0; i < prime.size(); i++) {
      local_decls_prime.push_back(std::move(prime.at(i)));
    }
    } else if (isFirstOfStmt(CurTok.type) ||
             CurTok.type == RBRA) { // FOLLOW(local_decls)
    // expand by local_decls ::= ε
  } else if (CurTok.type == VOID_TOK) {
    LogError(CurTok, "variables cannot have type 'void'");
  } else {
    LogError(CurTok, "expected a declaration, a statement or '}'");
  }

  return local_decls_prime;
}

// local_decl ::= var_type IDENT ";"
// var_type ::= "int"
//           |  "float"
//           |  "bool"
static std::unique_ptr<VarDeclAST> ParseLocalDecl() {
  TOKEN PrevTok;
  std::string Type;
  std::string Name = "";
  std::unique_ptr<VarDeclAST> local_decl;

  if (CurTok.type == INT_TOK || CurTok.type == FLOAT_TOK ||
      CurTok.type == BOOL_TOK) { // FIRST(var_type)
    PrevTok = CurTok;
    getNextToken(); // eat 'int' or 'float or 'bool'
    if (CurTok.type == IDENT) {
      Type = PrevTok.lexeme;
      Name = CurTok.getIdentifierStr(); // save the identifier name
      auto ident = std::make_unique<VariableASTnode>(CurTok, Name);
      local_decl = std::make_unique<VarDeclAST>(std::move(ident), Type);

      getNextToken(); // eat 'IDENT'
      if (CurTok.type != SC) {
        LogError(CurTok, "expected ';' to end local variable declaration");
        return nullptr;
      }
      getNextToken(); // eat ';'
    } else {
      LogError(CurTok, "expected a variable name in local variable declaration");
      return nullptr;
    }
  }
  return local_decl;
}

// local_decls ::= local_decl local_decls_prime
static std::vector<std::unique_ptr<VarDeclAST>> ParseLocalDecls() {
  std::vector<std::unique_ptr<VarDeclAST>> local_decls; // vector of local decls

  if (CurTok.type == INT_TOK || CurTok.type == FLOAT_TOK ||
      CurTok.type == BOOL_TOK) { // FIRST(local_decl)

    auto local_decl = ParseLocalDecl();
    if (local_decl) {
      local_decls.push_back(std::move(local_decl));
    }
    auto local_decls_prime = ParseLocalDeclsPrime();
    for (unsigned i = 0; i < local_decls_prime.size(); i++) {
      local_decls.push_back(std::move(local_decls_prime.at(i)));
    }
  
  // FOLLOW(local_decls)
  } else if (isFirstOfStmt(CurTok.type) || CurTok.type == RBRA) { 
    // expand by local_decls ::= epsilon
  } else if (CurTok.type == VOID_TOK) {
    LogError(CurTok, "variables cannot have type 'void'");
  } else {
    LogError(CurTok, "expected a declaration, a statement or '}'");
  }

  return local_decls;
}

// parse block
// block ::= "{" local_decls stmt_list "}"
static std::unique_ptr<ASTnode> ParseBlock() {
  std::vector<std::unique_ptr<VarDeclAST>> local_decls; // vector of local decls
  std::vector<std::unique_ptr<ASTnode>> stmt_list;      // vector of statements

  getNextToken(); // eat '{'

  local_decls = ParseLocalDecls();
  stmt_list = ParseStmtList();
  if (CurTok.type == RBRA)
    getNextToken(); // eat '}'
  else {            // syntax error
    LogError(CurTok, "expected '}' to close the block");
    return nullptr;
  }

  return std::make_unique<BlockAST>(std::move(local_decls), std::move(stmt_list));
}

// decl ::= var_decl
//       |  fun_decl
static std::unique_ptr<ASTnode> ParseDecl() {
  std::string IdName;
  std::vector<std::unique_ptr<ParamAST>> param_list;

  TOKEN PrevTok = CurTok; // to keep track of the type token

  if (CurTok.type == VOID_TOK || CurTok.type == INT_TOK ||
      CurTok.type == FLOAT_TOK || CurTok.type == BOOL_TOK) {
    getNextToken(); // eat the VOID_TOK, INT_TOK, BOOL_TOK or FLOAT_TOK

      if (CurTok.type == IDENT) {
        IdName = CurTok.getIdentifierStr(); // save the identifier name
      }

    if (CurTok.type == IDENT) {
      auto ident = std::make_unique<VariableASTnode>(CurTok, IdName);
      getNextToken(); // eat the IDENT
      if (CurTok.type ==
          SC) {         // found ';' then this is a global variable declaration.
        getNextToken(); // eat ;

        if (PrevTok.type != VOID_TOK)
          return std::make_unique<GlobVarDeclAST>(std::move(ident),
                                                  PrevTok.lexeme);
        else
          return LogError(PrevTok,
                          "variables cannot have type 'void'");
      } else if (CurTok.type ==
                 LPAR) { // found '(' then this is a function declaration.
        getNextToken();  // eat (

        auto P =
            ParseParams(); // parse the parameters, returns a vector of params
        // if (P.size() == 0) return nullptr;

        if (CurTok.type != RPAR) // syntax error
          return LogError(CurTok, "expected ')' in function declaration");

        getNextToken();          // eat )
        if (CurTok.type != LBRA) // syntax error
          return LogError(
              CurTok, "expected '{' in function declaration, function body");

        auto B = ParseBlock(); // parse the function body
        if (!B)
          return nullptr;

        // now create a Function prototype
        // create a Function body
        // put these to together
        // and return a std::unique_ptr<FunctionDeclAST>

        auto Proto = std::make_unique<FunctionPrototypeAST>(
            ident->getTok(), IdName, PrevTok.lexeme, std::move(P));
        return std::make_unique<FunctionDeclAST>(std::move(Proto),
                                                 std::move(B));
      } else
        return LogError(CurTok, "expected ';' or '(' after the declared name");
    } else
      return LogError(CurTok, "expected a name after the type");

  } else
    LogError(CurTok,
             "expected a declaration ('void', 'int', 'float' or 'bool')"); // syntax error

  return nullptr;
}

// decl_list ::= decl decl_list'
// decl_list' ::= decl decl_list' | epsilon
static std::vector<std::unique_ptr<ASTnode>> ParseDeclList() {
  std::vector<std::unique_ptr<ASTnode>> decls;
  decls.push_back(ParseDecl());

  // decl_list': keep parsing while the next token is in FIRST(decl)
  while (CurTok.type == VOID_TOK || CurTok.type == INT_TOK ||
         CurTok.type == FLOAT_TOK || CurTok.type == BOOL_TOK) {
    decls.push_back(ParseDecl());
  }

  // FOLLOW(decl_list') = { EOF }
  if (CurTok.type != EOF_TOK)
    LogError(CurTok, "expected a declaration ('void', 'int', 'float' or 'bool') or end of file");
  return decls;
}

// extern ::= "extern" type_spec IDENT "(" params ")" ";"
static std::unique_ptr<FunctionPrototypeAST> ParseExtern() {
  std::string IdName;
  TOKEN PrevTok;

  if (CurTok.type == EXTERN) {
    getNextToken(); // eat the EXTERN

    if (CurTok.type == VOID_TOK || CurTok.type == INT_TOK ||
        CurTok.type == FLOAT_TOK || CurTok.type == BOOL_TOK) {

      PrevTok = CurTok; // to keep track of the type token
      getNextToken();   // eat the VOID_TOK, INT_TOK, BOOL_TOK or FLOAT_TOK

      if (CurTok.type == IDENT) {
        IdName = CurTok.getIdentifierStr(); // save the identifier name
        auto ident = std::make_unique<VariableASTnode>(CurTok, IdName);
        getNextToken(); // eat the IDENT

        if (CurTok.type ==
            LPAR) {       // found '(' - this is an extern function declaration.
          getNextToken(); // eat (

          auto P =
              ParseParams(); // parse the parameters, returns a vector of params

          if (CurTok.type != RPAR) // syntax error
            return LogErrorP(
                CurTok, "expected ')' to close the extern parameter list");

          getNextToken(); // eat )

          if (CurTok.type == SC) {
            getNextToken(); // eat ";"
            auto Proto = std::make_unique<FunctionPrototypeAST>(
                ident->getTok(), IdName, PrevTok.lexeme, std::move(P));
            return Proto;
          } else
            return LogErrorP(
                CurTok,
                "expected ';' after extern declaration");
        } else
          return LogErrorP(CurTok,
                           "expected '(' after extern function name");
      } else
        return LogErrorP(CurTok, "expected a function name in extern declaration");

    } else
      LogErrorP(CurTok, "expected 'void', 'int', 'float' or 'bool' after 'extern'"); // syntax error
  }

  return nullptr;
}

// extern_list ::= extern extern_list'
// extern_list' ::= extern extern_list' | epsilon
static std::vector<std::unique_ptr<FunctionPrototypeAST>> ParseExternList() {
  std::vector<std::unique_ptr<FunctionPrototypeAST>> externs;
  externs.push_back(ParseExtern());

  // extern_list': keep parsing while the next token is in FIRST(extern)
  while (CurTok.type == EXTERN) {
    externs.push_back(ParseExtern());
  }

  // FOLLOW(extern_list') = FIRST(decl_list)
  if (CurTok.type != VOID_TOK && CurTok.type != INT_TOK &&
      CurTok.type != FLOAT_TOK && CurTok.type != BOOL_TOK)
    LogError(CurTok, "expected 'extern' or a declaration ('void', 'int', 'float' or 'bool')");
  return externs;
}

// program ::= extern_list decl_list | decl_list
static std::unique_ptr<ProgramAST> parser() {
  std::vector<std::unique_ptr<FunctionPrototypeAST>> externs;

  // FIRST(extern_list) selects the first alternative
  if (CurTok.type == EXTERN)
    externs = ParseExternList();

  auto decls = ParseDeclList();
  return std::make_unique<ProgramAST>(std::move(externs), std::move(decls));
}

//===----------------------------------------------------------------------===//
// Code Generation
//===----------------------------------------------------------------------===//

static LLVMContext TheContext;
static IRBuilder<> Builder(TheContext);
static std::unique_ptr<Module> TheModule;

// local variable scopes, innermost scope at the back
static std::vector<std::map<std::string, AllocaInst *>> Scopes;
// global variables 
static std::map<std::string, GlobalVariable *> GlobalNamedValues;
// return type of the function being generated
static llvm::Type *CurrentReturnType = nullptr;
// set so a function body shares the parameters's scope
static bool NextBlockIsFunctionBody = false;

/// token with no source position
static TOKEN noPosition() {
  TOKEN t;
  t.lineNo = 0;
  t.columnNo = 0;
  return t;
}

/// LogSemanticError - report a semantic error (e.g. Type, Scope) and stop
static Value *LogSemanticError(const TOKEN &tok, const std::string &kind, const std::string &msg) {
  if (tok.lineNo > 0)
    fprintf(stderr, "%d:%d %s error: %s\n", tok.lineNo, tok.columnNo, kind.c_str(), msg.c_str());
  else
    fprintf(stderr, "%s error: %s\n", kind.c_str(), msg.c_str());
  exit(2);
  return nullptr;
}

/// MiniC type name to LLVM type
static llvm::Type *getLLVMType(const std::string &name) {
  if (name == "int")
    return llvm::Type::getInt32Ty(TheContext);
  if (name == "float")
    return llvm::Type::getFloatTy(TheContext);
  if (name == "bool")
    return llvm::Type::getInt1Ty(TheContext);
  return llvm::Type::getVoidTy(TheContext);
}

/// MiniC name of an LLVM type (for error messages)
static std::string typeName(llvm::Type *T) {
  if (T->isIntegerTy(1))
    return "bool";
  if (T->isIntegerTy(32))
    return "int";
  if (T->isFloatTy())
    return "float";
  if (T->isVoidTy())
    return "void";
  return "unknown";
}

/// orders bool < int < float to find the wider type
static int typeRank(llvm::Type *T) {
  if (T->isIntegerTy(1))
    return 1;
  if (T->isIntegerTy(32))
    return 2;
  if (T->isFloatTy())
    return 3;
  return 0;
}

/// zero constant of a type- used to initialise variables
static Constant *zeroValue(llvm::Type *T) {
  if (T->isFloatTy())
    return ConstantFP::get(T, 0.0);
  return ConstantInt::get(T, 0);
}

/// widen - convert V to type T if widening, otherwise its a semantic error
static Value *widen(Value *V, llvm::Type *T, const TOKEN &tok, const std::string &context) {
  llvm::Type *From = V->getType();
  if (From == T)
    return V;
  if (From->isVoidTy())
    return LogSemanticError(tok, "Type", "void value used in " + context);
  // bool to int
  if (From->isIntegerTy(1) && T->isIntegerTy(32))
    return Builder.CreateZExt(V, T, "booltoint");
  // bool to float
  if (From->isIntegerTy(1) && T->isFloatTy())
    return Builder.CreateUIToFP(V, T, "booltofloat");
  // int to float
  if (From->isIntegerTy(32) && T->isFloatTy())
    return Builder.CreateSIToFP(V, T, "inttofloat");
  return LogSemanticError(tok, "Type", "narrowing conversion from " + typeName(From) + " to " + typeName(T) + " in " + context + " is not allowed");
}

/// toBool - any scalar to bool by comparing with zero
static Value *toBool(Value *V, const TOKEN &tok, const std::string &context) {
  llvm::Type *T = V->getType();
  if (T->isIntegerTy(1))
    return V;
  if (T->isIntegerTy(32))
    return Builder.CreateICmpNE(V, ConstantInt::get(T, 0), "tobool");
  if (T->isFloatTy())
    return Builder.CreateFCmpUNE(V, ConstantFP::get(T, 0.0), "tobool");
  return LogSemanticError(tok, "Type", "void value used as a condition in " + context);
}

static Value *lookupVariable(const std::string &name, llvm::Type *&type) {
  for (auto it = Scopes.rbegin(); it != Scopes.rend(); ++it) {
    auto found = it->find(name);
    if (found != it->end()) {
      type = found->second->getAllocatedType();
      return found->second;
    }
  }
  auto g = GlobalNamedValues.find(name);
  if (g != GlobalNamedValues.end()) {
    type = g->second->getValueType();
    return g->second;
  }
  return nullptr;
}

/// true if the block already ends in a branch or return
static bool blockEnded(BasicBlock *BB) {
  return !BB->empty() && BB->back().isTerminator();
}

// stack slot in the functions entry block
static AllocaInst *CreateEntryBlockAlloca(Function *F, llvm::Type *T, const std::string &name) {
  IRBuilder<> TmpB(&F->getEntryBlock(), F->getEntryBlock().begin());
  return TmpB.CreateAlloca(T, nullptr, name);
}

// expressions

Value *IntASTnode::codegen() {
  return ConstantInt::get(llvm::Type::getInt32Ty(TheContext), Val, true);
}

Value *FloatASTnode::codegen() {
  return ConstantFP::get(llvm::Type::getFloatTy(TheContext), Val);
}

Value *BoolASTnode::codegen() {
  return ConstantInt::get(llvm::Type::getInt1Ty(TheContext), Bool ? 1 : 0);
}

Value *VariableASTnode::codegen() {
  llvm::Type *T = nullptr;
  Value *Ptr = lookupVariable(Name, T);
  if (!Ptr)
    return LogSemanticError(Tok, "Scope", "use of undeclared variable '" + Name + "'");
  return Builder.CreateLoad(T, Ptr, Name);
}

Value *UnaryExprAST::codegen() {
  Value *V = Operand->codegen();
  if (OpTok.type == NOT)
    return Builder.CreateNot(toBool(V, OpTok, "operator '!'"), "nottmp");

  // unary minus, bool promoted to int first 
  if (V->getType()->isIntegerTy(1))
    V = widen(V, llvm::Type::getInt32Ty(TheContext), OpTok, "unary '-'");
  if (V->getType()->isFloatTy())
    return Builder.CreateFNeg(V, "negtmp");
  if (V->getType()->isIntegerTy(32))
    return Builder.CreateNeg(V, "negtmp");
  return LogSemanticError(OpTok, "Type", "invalid operand of type " + typeName(V->getType()) + " to unary '-'");
}

/// && and || with short-circuit evaluation
static Value *codegenLogical(const TOKEN &OpTok, ASTnode *Left, ASTnode *Right) {
  bool IsAnd = (OpTok.type == AND);
  std::string Ctx = "operator '" + OpTok.lexeme + "'";
  llvm::Type *BoolTy = llvm::Type::getInt1Ty(TheContext);

  Value *L = toBool(Left->codegen(), OpTok, Ctx);
  Function *F = Builder.GetInsertBlock()->getParent();
  BasicBlock *LeftEnd = Builder.GetInsertBlock();
  BasicBlock *RightBB = BasicBlock::Create(TheContext, IsAnd ? "and.rhs" : "or.rhs", F);
  BasicBlock *MergeBB = BasicBlock::Create(TheContext, IsAnd ? "and.end" : "or.end", F);

  // && only evaluates the right side if the left is true, || only if it is false
  if (IsAnd)
    Builder.CreateCondBr(L, RightBB, MergeBB);
  else
    Builder.CreateCondBr(L, MergeBB, RightBB);

  Builder.SetInsertPoint(RightBB);
  Value *R = toBool(Right->codegen(), OpTok, Ctx);
  BasicBlock *RightEnd = Builder.GetInsertBlock();
  Builder.CreateBr(MergeBB);

  Builder.SetInsertPoint(MergeBB);
  PHINode *PN = Builder.CreatePHI(BoolTy, 2, IsAnd ? "andtmp" : "ortmp");
  // right side skipped, so && is false and || is true
  PN->addIncoming(ConstantInt::get(BoolTy, IsAnd ? 0 : 1), LeftEnd);
  PN->addIncoming(R, RightEnd);
  return PN;
}

Value *BinaryExprAST::codegen() {
  if (OpTok.type == AND || OpTok.type == OR)
    return codegenLogical(OpTok, Left.get(), Right.get());

  Value *L = Left->codegen();
  Value *R = Right->codegen();
  std::string Ctx = "operator '" + OpTok.lexeme + "'";
  if (L->getType()->isVoidTy() || R->getType()->isVoidTy())
    return LogSemanticError(OpTok, "Type", "void value used in " + Ctx);

  // widen both operands to the wider type, bool promoted to at least int
  llvm::Type *T = typeRank(L->getType()) >= typeRank(R->getType()) ? L->getType() : R->getType();
  if (T->isIntegerTy(1))
    T = llvm::Type::getInt32Ty(TheContext);
  L = widen(L, T, OpTok, Ctx);
  R = widen(R, T, OpTok, Ctx);
  bool IsFloat = T->isFloatTy();

    switch (OpTok.type) {
  case PLUS:
    if (IsFloat)
      return Builder.CreateFAdd(L, R, "addtmp");
    return Builder.CreateAdd(L, R, "addtmp");
  case MINUS:
    if (IsFloat)
      return Builder.CreateFSub(L, R, "subtmp");
    return Builder.CreateSub(L, R, "subtmp");
  case ASTERIX:
    if (IsFloat)
      return Builder.CreateFMul(L, R, "multmp");
    return Builder.CreateMul(L, R, "multmp");
  case DIV:
    if (IsFloat)
      return Builder.CreateFDiv(L, R, "divtmp");
    return Builder.CreateSDiv(L, R, "divtmp");
  case MOD:
    // only for integers, as in C
    if (IsFloat)
      return LogSemanticError(OpTok, "Type", "operator '%' requires int operands, not float");
    return Builder.CreateSRem(L, R, "modtmp");
  case EQ:
    if (IsFloat)
      return Builder.CreateFCmpUEQ(L, R, "eqtmp");
    return Builder.CreateICmpEQ(L, R, "eqtmp");
  case NE:
    if (IsFloat)
      return Builder.CreateFCmpUNE(L, R, "netmp");
    return Builder.CreateICmpNE(L, R, "netmp");
  case LT:
    if (IsFloat)
      return Builder.CreateFCmpULT(L, R, "lttmp");
    return Builder.CreateICmpSLT(L, R, "lttmp");
  case LE:
    if (IsFloat)
      return Builder.CreateFCmpULE(L, R, "letmp");
    return Builder.CreateICmpSLE(L, R, "letmp");
  case GT:
    if (IsFloat)
      return Builder.CreateFCmpUGT(L, R, "gttmp");
    return Builder.CreateICmpSGT(L, R, "gttmp");
  case GE:
    if (IsFloat)
      return Builder.CreateFCmpUGE(L, R, "getmp");
    return Builder.CreateICmpSGE(L, R, "getmp");
  }
  return LogSemanticError(OpTok, "Internal", "unknown binary operator '" + OpTok.lexeme + "'");
}

Value *AssignExprAST::codegen() {
  llvm::Type *T = nullptr;
  Value *Ptr = lookupVariable(Var->getName(), T);
  if (!Ptr)
    return LogSemanticError(Var->getTok(), "Scope", "assignment to undeclared variable '" + Var->getName() + "'");
  Value *V = widen(RHS->codegen(), T, Var->getTok(), "assignment to '" + Var->getName() + "'");
  Builder.CreateStore(V, Ptr);
  // value of the assignment is the stored value, so a = b = c works
  return V;
}

Value *ArgsAST::codegen() {
  Function *F = TheModule->getFunction(Callee);
  if (!F)
    return LogSemanticError(Tok, "Scope", "call to undeclared function '" + Callee + "'");
  // argument count must match exactly
  if (F->arg_size() != ArgsList.size())
    return LogSemanticError(Tok, "Arity", "function '" + Callee + "' expects " +
                                     std::to_string(F->arg_size()) + " argument(s) but " +
                                     std::to_string(ArgsList.size()) + " were given");
  std::vector<Value *> Args;
  unsigned i = 0;
  for (auto &Arg : F->args()) {
    Value *V = ArgsList[i]->codegen();
    Args.push_back(widen(V, Arg.getType(), Tok,
                         "argument " + std::to_string(i + 1) + " of call to '" + Callee + "'"));
    i++;
  }
  // a void call has no value, so it can't be named
  if (F->getReturnType()->isVoidTy())
    return Builder.CreateCall(F, Args);
  return Builder.CreateCall(F, Args, "calltmp");
}

// ---------- statements ----------

Value *ReturnAST::codegen() {
  if (!Val) {
    if (!CurrentReturnType->isVoidTy())
      return LogSemanticError(RetTok, "Type", "non-void function must return a value of type " + typeName(CurrentReturnType));
    Builder.CreateRetVoid();
  } else {
    if (CurrentReturnType->isVoidTy())
      return LogSemanticError(RetTok, "Type", "void function cannot return a value");
    Builder.CreateRet(widen(Val->codegen(), CurrentReturnType, RetTok, "return statement"));
  }
  return nullptr;
}

Value *IfExprAST::codegen() {
  Value *C = toBool(Cond->codegen(), noPosition(), "if statement");
  Function *F = Builder.GetInsertBlock()->getParent();
  BasicBlock *ThenBB = BasicBlock::Create(TheContext, "then", F);
  BasicBlock *ElseBB = Else ? BasicBlock::Create(TheContext, "else", F) : nullptr;
  BasicBlock *MergeBB = BasicBlock::Create(TheContext, "ifcont", F);
  Builder.CreateCondBr(C, ThenBB, Else ? ElseBB : MergeBB);

  Builder.SetInsertPoint(ThenBB);
  Then->codegen();
  // only jump to the merge block if the branch didn't already end
  if (!Builder.GetInsertBlock()->getTerminator())
    Builder.CreateBr(MergeBB);

  if (Else) {
    Builder.SetInsertPoint(ElseBB);
    Else->codegen();
    if (!Builder.GetInsertBlock()->getTerminator())
      Builder.CreateBr(MergeBB);
  }
  Builder.SetInsertPoint(MergeBB);
  return nullptr;
}

Value *WhileExprAST::codegen() {
  Function *F = Builder.GetInsertBlock()->getParent();
  BasicBlock *CondBB = BasicBlock::Create(TheContext, "while.cond", F);
  BasicBlock *BodyBB = BasicBlock::Create(TheContext, "while.body", F);
  BasicBlock *EndBB = BasicBlock::Create(TheContext, "while.end", F);
  Builder.CreateBr(CondBB);

  // loop header, condition tested every iteration
  Builder.SetInsertPoint(CondBB);
  Value *C = toBool(Cond->codegen(), noPosition(), "while statement");
  Builder.CreateCondBr(C, BodyBB, EndBB);

  Builder.SetInsertPoint(BodyBB);
  Body->codegen();
  // jump back to the header unless the body already ended
  if (!Builder.GetInsertBlock()->getTerminator())
    Builder.CreateBr(CondBB);

  Builder.SetInsertPoint(EndBB);
  return nullptr;
}

Value *BlockAST::codegen() {
  // function body shares the parameters' scope, other blocks open a new one
  bool OwnScope = !NextBlockIsFunctionBody;
  NextBlockIsFunctionBody = false;
  if (OwnScope)
    Scopes.emplace_back();

  for (auto &D : LocalDecls)
    D->codegen();
  for (auto &S : Stmts) {
    S->codegen();
    // stop after a return, the rest of the block is unreachable
    if (Builder.GetInsertBlock()->getTerminator())
      break;
  }

  if (OwnScope)
    Scopes.pop_back();
  return nullptr;
}

// ---------- declarations ----------

Value *VarDeclAST::codegen() {
  const std::string &Name = Var->getName();
  // shadowing is allowed, redeclaring in the same scope is not
  if (Scopes.back().count(Name))
    return LogSemanticError(Var->getTok(), "Scope", "redeclaration of variable '" + Name + "' in the same scope");
  llvm::Type *T = getLLVMType(Type);
  Function *F = Builder.GetInsertBlock()->getParent();
  AllocaInst *A = CreateEntryBlockAlloca(F, T, Name);
  // locals start at zero
  Builder.CreateStore(zeroValue(T), A);
  Scopes.back()[Name] = A;
  return nullptr;
}

Value *GlobVarDeclAST::codegen() {
  const std::string &Name = Var->getName();
  // globals can only be defined once
  if (GlobalNamedValues.count(Name) || TheModule->getFunction(Name))
    return LogSemanticError(Var->getTok(), "Scope", "redefinition of global '" + Name + "'");
  llvm::Type *T = getLLVMType(Type);
  // common linkage and a zero value as in the LLVM IR lecture, globals start at 0
  auto *G = new GlobalVariable(*TheModule, T, false, GlobalValue::CommonLinkage,
                               Constant::getNullValue(T), Name);
  G->setAlignment(MaybeAlign(4));
  GlobalNamedValues[Name] = G;
  return G;
}

Function *FunctionPrototypeAST::codegen() {
  std::vector<llvm::Type *> ParamTypes;
  for (auto &P : Params)
    ParamTypes.push_back(getLLVMType(P->getType()));
  FunctionType *FT = FunctionType::get(getLLVMType(Type), ParamTypes, false);

  // redeclaring a function (e.g. extern then definition) needs the same signature
  if (Function *Existing = TheModule->getFunction(Name)) {
    if (Existing->getFunctionType() != FT) {
      LogSemanticError(Tok, "Type", "conflicting declarations of function '" + Name + "'");
      return nullptr;
    }
    return Existing;
  }
  if (GlobalNamedValues.count(Name)) {
    LogSemanticError(Tok, "Scope", "'" + Name + "' is already declared as a global variable");
    return nullptr;
  }

  Function *F = Function::Create(FT, Function::ExternalLinkage, Name, TheModule.get());
  unsigned i = 0;
  for (auto &Arg : F->args())
    Arg.setName(Params[i++]->getName());
  return F;
}

Value *FunctionDeclAST::codegen() {
  Function *F = Proto->codegen();
  // only one body per function
  if (!F->empty())
    return LogSemanticError(Proto->getTok(), "Scope", "redefinition of function '" + Proto->getName() + "'");

  BasicBlock *Entry = BasicBlock::Create(TheContext, "entry", F);
  Builder.SetInsertPoint(Entry);
  CurrentReturnType = F->getReturnType();

  // parameters live in the function body scope
  Scopes.clear();
  Scopes.emplace_back();
  unsigned i = 0;
  for (auto &Arg : F->args()) {
    std::string ArgName = Proto->getParams()[i++]->getName();
    if (Scopes.back().count(ArgName))
      return LogSemanticError(Proto->getTok(), "Scope", "duplicate parameter '" + ArgName + "' in function '" + Proto->getName() + "'");
    AllocaInst *A = CreateEntryBlockAlloca(F, Arg.getType(), ArgName);
    Builder.CreateStore(&Arg, A);
    Scopes.back()[ArgName] = A;
  }

  NextBlockIsFunctionBody = true;
  Block->codegen();

  // every block needs a terminator, therefore a default return
  if (!blockEnded(Builder.GetInsertBlock())) {
    if (CurrentReturnType->isVoidTy())
      Builder.CreateRetVoid();
    else
      Builder.CreateRet(zeroValue(CurrentReturnType));
  }
  Scopes.clear();

  // check that the generated function is valid
  if (verifyFunction(*F, &errs()))
    return LogSemanticError(noPosition()), "Internal", "invalid IR generated for function '" + Proto->getName() + "'");
  return F;
}

Value *ProgramAST::codegen() {
  for (auto &E : Externs)
    E->codegen();
  for (auto &D : Decls)
    D->codegen();
  return nullptr;
}

//===----------------------------------------------------------------------===//
// AST Printer
//===----------------------------------------------------------------------===//

// void IntASTnode::display(int tabs) {
//   printf("%s\n",getType().c_str());
// }

//===----------------------------------------------------------------------===//
// Main driver code.
//===----------------------------------------------------------------------===//

int main(int argc, char **argv) {
  if (argc == 2) {
    pFile = fopen(argv[1], "r");
    if (pFile == NULL) {
      perror("Error opening file");
      return 1;
    }
  } else {
    std::cout << "Usage: ./code InputFile\n";
    return 1;
  }

  // initialize line number and column numbers to zero
  lineNo = 1;
  columnNo = 1;

  // get the first token
  getNextToken();

  // Make the module, which holds all the code.
  TheModule = std::make_unique<Module>("mini-c", TheContext);

  // Run the parser now.

  /* UNCOMMENT : Task 2 - Parser */
  auto Root = parser();
  // print the whole AST to stdout
  llvm::outs() << *Root << "\n";

  // generate IR for the whole program, semantic errors exit
  Root->codegen();

  // check the whole module is valid IR before writing it
  if (verifyModule(*TheModule, &errs())) {
    fprintf(stderr, "Internal error: generated IR is invalid\n");
    return 1;
  }

  printf(
      "********************* FINAL IR (begin) ****************************\n");
  // Print out all of the generated code into a file called output.ll
  // printf("%s\n", argv[1]);
  auto Filename = "output.ll";
  std::error_code EC;
  raw_fd_ostream dest(Filename, EC, sys::fs::OF_None);

  if (EC) {
    errs() << "Could not open file: " << EC.message();
    return 1;
  }
  // TheModule->print(errs(), nullptr); // print IR to terminal
  TheModule->print(dest, nullptr);
  printf(
      "********************* FINAL IR (end) ******************************\n");

  fclose(pFile); // close the file that contains the code that was parsed
  return 0;
}