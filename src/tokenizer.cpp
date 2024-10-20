#include "tokenizer.h"

#include <string_view>

Tokenizer::Tokenizer(const YALMData& data, int bos_id, int eos_id) {
  this->bos_id = bos_id;
  this->eos_id = eos_id;
  // TODO figure out edge cases:
  // Q: should `vocab` include byte fallback tokens?
  // Q: should `vocab` include special tokens, e.g. '<unk>', '<s>', '</s>'?
  for (auto& val : data.metadata.at("tokenizer.tokens")) {
    vocab.push_back(val.get<std::string>());
  }
  for (int i = 0; i < vocab.size(); i++) {
    if (vocab[i] == "<0x00>") {
      byte_fallback_start = i;
    } else if (vocab[i] == "<|eot_id|>" || vocab[i] == "<|end|>" || vocab[i] == "<|im_end|>") {
      eot_id = i;
    }
  }
  // init byte_pieces
  for (int i = 0; i < 256; i++) {
    byte_pieces[i] = (char)i;
  }
  // init vocab trie
  for (int i = 0; i < vocab.size(); i++) {
    const std::string& word = vocab[i];
    TokenTrie& p = vocab_trie;
    for (char c : word) {
      if (p.children.count(c) == 0) {
        p.children[c] = {};
      }
      p = p.children[c];
    }
    p.token_id = i;
  }
}

std::string Tokenizer::decode_one(int prev_token, int token) const {
  const std::string& piece = vocab[token];
  // if following BOS token, sentencepiece decoder strips any leading whitespace
  if (prev_token == bos_id && piece[0] == ' ') {
    return piece.substr(1);
  }
  // return byte piece for byte fallback tokens (<0x00>, <0x01>, ..., <0xFF>)
  if (byte_fallback_start >= 0 && token >= byte_fallback_start && (token - byte_fallback_start) < 256) {
    return byte_pieces[token - byte_fallback_start];
  }
  return piece;
}

std::vector<int> Tokenizer::encode(const std::string& text) const {
  std::vector<int> out_tokens;
  // TODO: handle BOS token (pass optional flag)

  for (int i = 0; i < text.size();) {
    int l = 0;
    int valid_l = 0;
    TokenTrie& p = vocab_trie;
    TokenTrie* valid_p = nullptr;
    while (i + l < text.size()) {
      char c = text[i+l];
      if (p.children.count(c)) {
        p = p.children[c];
        l += 1;
        if (p.token_id >= 0) {
          valid_p = &p;
          valid_l = l;
        }
      } else {
        break;
      }
    }
    if (!valid_p) {
      // No substring starting from `i` matches any vocab words, use byte fallback
      if (byte_fallback_start >= 0) {
        out_tokens.push_back((unsigned char)text[i] + byte_fallback_start);
      }
      i += 1;
    } else {
      out_tokens.push_back(valid_p->token_id);
      i += valid_l;
    }
  }

  // TODO: handle EOS token (pass optional flag)

  return out_tokens;
}