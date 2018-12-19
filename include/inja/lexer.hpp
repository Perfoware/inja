#ifndef PANTOR_INJA_LEXER_HPP
#define PANTOR_INJA_LEXER_HPP

#include <inja/config.hpp>
#include <inja/token.hpp>
#include <inja/utils.hpp>


namespace inja {

class Lexer {
 public:
  explicit Lexer(const LexerConfig& config) : m_config(config) {}

  void start(std::string_view in) {
    m_in = in;
    m_tokStart = 0;
    m_pos = 0;
    m_state = State::Text;
  }

  Token scan() {
    m_tokStart = m_pos;

  again:
    if (m_tokStart >= m_in.size()) return make_token(Token::kEof);

    switch (m_state) {
      default:
      case State::Text: {
        // fast-scan to first open character
        size_t openStart = m_in.substr(m_pos).find_first_of(m_config.openChars);
        if (openStart == std::string_view::npos) {
          // didn't find open, return remaining text as text token
          m_pos = m_in.size();
          return make_token(Token::kText);
        }
        m_pos += openStart;

        // try to match one of the opening sequences, and get the close
        std::string_view openStr = m_in.substr(m_pos);
        if (string_view_starts_with(openStr, m_config.expressionOpen)) {
          m_state = State::ExpressionStart;
        } else if (string_view_starts_with(openStr, m_config.statementOpen)) {
          m_state = State::StatementStart;
        } else if (string_view_starts_with(openStr, m_config.commentOpen)) {
          m_state = State::CommentStart;
        } else if ((m_pos == 0 || m_in[m_pos - 1] == '\n') &&
                   string_view_starts_with(openStr, m_config.lineStatement)) {
          m_state = State::LineStart;
        } else {
          ++m_pos; // wasn't actually an opening sequence
          goto again;
        }
        if (m_pos == m_tokStart) goto again;  // don't generate empty token
        return make_token(Token::kText);
      }
      case State::ExpressionStart: {
        m_state = State::ExpressionBody;
        m_pos += m_config.expressionOpen.size();
        return make_token(Token::kExpressionOpen);
      }
      case State::LineStart: {
        m_state = State::LineBody;
        m_pos += m_config.lineStatement.size();
        return make_token(Token::kLineStatementOpen);
      }
      case State::StatementStart: {
        m_state = State::StatementBody;
        m_pos += m_config.statementOpen.size();
        return make_token(Token::kStatementOpen);
      }
      case State::CommentStart: {
        m_state = State::CommentBody;
        m_pos += m_config.commentOpen.size();
        return make_token(Token::kCommentOpen);
      }
      case State::ExpressionBody:
        return scan_body(m_config.expressionClose, Token::kExpressionClose);
      case State::LineBody:
        return scan_body("\n", Token::kLineStatementClose);
      case State::StatementBody:
        return scan_body(m_config.statementClose, Token::kStatementClose);
      case State::CommentBody: {
        // fast-scan to comment close
        size_t end = m_in.substr(m_pos).find(m_config.commentClose);
        if (end == std::string_view::npos) {
          m_pos = m_in.size();
          return make_token(Token::kEof);
        }
        // return the entire comment in the close token
        m_state = State::Text;
        m_pos += end + m_config.commentClose.size();
        return make_token(Token::kCommentClose);
      }
    }
  }

  const LexerConfig& get_config() const { return m_config; }

 private:
  Token scan_body(std::string_view close, Token::Kind closeKind) {
  again:
    // skip whitespace (except for \n as it might be a close)
    if (m_tokStart >= m_in.size()) return make_token(Token::kEof);
    char ch = m_in[m_tokStart];
    if (ch == ' ' || ch == '\t' || ch == '\r') {
      ++m_tokStart;
      goto again;
    }

    // check for close
    if (string_view_starts_with(m_in.substr(m_tokStart), close)) {
      m_state = State::Text;
      m_pos = m_tokStart + close.size();
      return make_token(closeKind);
    }

    // skip \n
    if (ch == '\n') {
      ++m_tokStart;
      goto again;
    }

    m_pos = m_tokStart + 1;
    if (std::isalpha(ch)) return scan_id();
    switch (ch) {
      case ',':
        return make_token(Token::kComma);
      case ':':
        return make_token(Token::kColon);
      case '(':
        return make_token(Token::kLeftParen);
      case ')':
        return make_token(Token::kRightParen);
      case '[':
        return make_token(Token::kLeftBracket);
      case ']':
        return make_token(Token::kRightBracket);
      case '{':
        return make_token(Token::kLeftBrace);
      case '}':
        return make_token(Token::kRightBrace);
      case '>':
        if (m_pos < m_in.size() && m_in[m_pos] == '=') {
          ++m_pos;
          return make_token(Token::kGreaterEqual);
        }
        return make_token(Token::kGreaterThan);
      case '<':
        if (m_pos < m_in.size() && m_in[m_pos] == '=') {
          ++m_pos;
          return make_token(Token::kLessEqual);
        }
        return make_token(Token::kLessThan);
      case '=':
        if (m_pos < m_in.size() && m_in[m_pos] == '=') {
          ++m_pos;
          return make_token(Token::kEqual);
        }
        return make_token(Token::kUnknown);
      case '!':
        if (m_pos < m_in.size() && m_in[m_pos] == '=') {
          ++m_pos;
          return make_token(Token::kNotEqual);
        }
        return make_token(Token::kUnknown);
      case '\"':
        return scan_string();
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
      case '-':
        return scan_number();
      case '_':
        return scan_id();
      default:
        return make_token(Token::kUnknown);
    }
  }

  Token scan_id() {
    for (;;) {
      if (m_pos >= m_in.size()) break;
      char ch = m_in[m_pos];
      if (!isalnum(ch) && ch != '.' && ch != '/' && ch != '_' && ch != '-') break;
      ++m_pos;
    }
    return make_token(Token::kId);
  }

  Token scan_number() {
    for (;;) {
      if (m_pos >= m_in.size()) break;
      char ch = m_in[m_pos];
      // be very permissive in lexer (we'll catch errors when conversion happens)
      if (!isdigit(ch) && ch != '.' && ch != 'e' && ch != 'E' && ch != '+' && ch != '-')
        break;
      ++m_pos;
    }
    return make_token(Token::kNumber);
  }

  Token scan_string() {
    bool escape = false;
    for (;;) {
      if (m_pos >= m_in.size()) break;
      char ch = m_in[m_pos++];
      if (ch == '\\')
        escape = true;
      else if (!escape && ch == m_in[m_tokStart])
        break;
      else
        escape = false;
    }
    return make_token(Token::kString);
  }

  Token make_token(Token::Kind kind) const {
    return Token(kind, string_view_slice(m_in, m_tokStart, m_pos));
  }

  const LexerConfig& m_config;
  std::string_view m_in;
  size_t m_tokStart;
  size_t m_pos;
  enum class State {
    Text,
    ExpressionStart,
    ExpressionBody,
    LineStart,
    LineBody,
    StatementStart,
    StatementBody,
    CommentStart,
    CommentBody
  };
  State m_state;
};

}

#endif // PANTOR_INJA_LEXER_HPP
