#include "lexer.h"
#include <unordered_map>
#include <stdexcept>

static const std::unordered_map<std::string, TokenType> keywords = {
  {"var", TokenType::VAR}, {"print", TokenType::PRINT}, {"show", TokenType::PRINT},
  {"if", TokenType::IF}, {"else", TokenType::ELSE},
  {"while", TokenType::WHILE}, {"for", TokenType::FOR},
  {"fun", TokenType::FUN}, {"return", TokenType::RETURN},
  {"break", TokenType::BREAK}, {"continue", TokenType::CONTINUE},
  {"true", TokenType::TRUE}, {"false", TokenType::FALSE},
  {"nil", TokenType::NIL},
  {"and", TokenType::AND}, {"or", TokenType::OR},
  {"in", TokenType::IN},
  {"let", TokenType::VAR}, {"fn", TokenType::FUN},
  {"unless", TokenType::UNLESS}, {"loop", TokenType::LOOP},
  {"is", TokenType::IS}, {"isnt", TokenType::ISNT},
};

std::vector<Token> Lexer::scan() {
  while (!isAtEnd()) {
    start = current;
    scanToken();
  }
  tokens.push_back({TokenType::EOF_, "", line});
  return tokens;
}

bool Lexer::isAtEnd() { return current >= (int)source.size(); }

char Lexer::advance() { return source[current++]; }

bool Lexer::match(char expected) {
  if (isAtEnd() || source[current] != expected) return false;
  current++;
  return true;
}

char Lexer::peek() { return isAtEnd() ? '\0' : source[current]; }
char Lexer::peekNext() { return current + 1 >= (int)source.size() ? '\0' : source[current + 1]; }

void Lexer::addToken(TokenType type) {
  tokens.push_back({type, source.substr(start, current - start), line});
}

void Lexer::addToken(TokenType type, std::string value) {
  tokens.push_back({type, value, line});
}

void Lexer::scanToken() {
  char c = advance();
  switch (c) {
    case '(': addToken(TokenType::LPAREN); break;
    case ')': addToken(TokenType::RPAREN); break;
    case '[': addToken(TokenType::LBRACKET); break;
    case ']': addToken(TokenType::RBRACKET); break;
    case '{': addToken(TokenType::LBRACE); break;
    case '}': addToken(TokenType::RBRACE); break;
    case ';': addToken(TokenType::SEMICOLON); break;
    case ',': addToken(TokenType::COMMA); break;
    case '.': addToken(TokenType::DOT); break;
    case '?': addToken(TokenType::QUESTION); break;
    case ':': addToken(TokenType::COLON); break;
    case '+': addToken(match('=') ? TokenType::PLUS_EQ : TokenType::PLUS); break;
    case '-': addToken(match('=') ? TokenType::MINUS_EQ : TokenType::MINUS); break;
    case '*': addToken(match('=') ? TokenType::STAR_EQ : TokenType::STAR); break;
    case '%': addToken(match('=') ? TokenType::PERCENT_EQ : TokenType::PERCENT); break;
    case '/':
      if (match('/')) { while (peek() != '\n' && !isAtEnd()) advance(); }
      else { addToken(match('=') ? TokenType::SLASH_EQ : TokenType::SLASH); }
      break;
    case '=': addToken(match('>') ? TokenType::ARROW : match('=') ? TokenType::EQ_EQ : TokenType::EQ); break;
    case '!': addToken(match('=') ? TokenType::BANG_EQ : TokenType::BANG); break;
    case '<': addToken(match('=') ? TokenType::LT_EQ : TokenType::LT); break;
    case '>': addToken(match('=') ? TokenType::GT_EQ : TokenType::GT); break;
    case ' ': case '\r': case '\t': break;
    case '\n': line++; break;
    case '"': string_(); break;
    default:
      if (isDigit(c)) number();
      else if (isAlpha(c)) identifier();
      else throw std::runtime_error(std::string("Unexpected char '") + c + "' at line " + std::to_string(line));
  }
}

void Lexer::string_() {
  while (peek() != '"' && !isAtEnd()) {
    if (peek() == '\n') line++;
    advance();
  }
  if (isAtEnd()) throw std::runtime_error("Unterminated string at line " + std::to_string(line));
  advance();
  addToken(TokenType::STRING, source.substr(start + 1, current - start - 2));
}

void Lexer::number() {
  while (isDigit(peek())) advance();
  if (peek() == '.' && isDigit(peekNext())) {
    advance();
    while (isDigit(peek())) advance();
  }
  addToken(TokenType::NUMBER, source.substr(start, current - start));
}

void Lexer::identifier() {
  while (isAlphaNumeric(peek())) advance();
  std::string text = source.substr(start, current - start);
  auto it = keywords.find(text);
  addToken(it != keywords.end() ? it->second : TokenType::IDENTIFIER);
}

bool Lexer::isDigit(char c) { return c >= '0' && c <= '9'; }
bool Lexer::isAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool Lexer::isAlphaNumeric(char c) { return isAlpha(c) || isDigit(c); }
