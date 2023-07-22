#include "llama.h"

#include <cstdio>
#include <string>
#include <map>
#include <vector>

static std::string escape_whitespace(const std::string& text) {
    std::string result;
    bool escaping = false;
    result += char(0xe2);
    result += char(0x96);
    result += char(0x81);
    for (size_t offs = 0; offs < text.length(); ++offs) {
        if (text[offs] == ' ' || text[offs] == '\t' || text[offs] == '\n') {
            if (!escaping) {
                result += char(0xe2);
                result += char(0x96);
                result += char(0x81);
                escaping = true;
            }
        }
        else {
            escaping = false;
            result += text[offs];
        }
    }
    return result;
}

static std::string unescape_whitespace(llama_context* ctx, llama_token token) {
    const char* word = llama_token_to_str(ctx, token);
    if (strlen(word) >= 3 &&
        word[0] == char(0xe2) &&
        word[1] == char(0x96) &&
        word[2] == char(0x81)) {
        return std::string(" ") + (word + 3);
    } 
    return word;
}

static std::string unescape_whitespace(llama_context* ctx, const llama_token* tokens, int count) {
    std::string result;
    for (int i = 0; i < count; ++i) {
        result += unescape_whitespace(ctx, tokens[i]);
    }
    return result;
}

static const std::map<std::string, std::vector<llama_token>> & k_tests()
{
    static std::map<std::string, std::vector<llama_token>> _k_tests = {
        { "Hello world",        { 1,  15043,   3186, }, },
        { " Hello world",       { 1,  29871,  15043,   3186, }, },
        { "Hello World",        { 1,  15043,   2787, }, },
        { " Hello World",       { 1,  29871,  15043,   2787, }, },
        {" Hello World!",       { 1,  29871,  15043,   2787,  29991, }, },
        {" this is 🦙.cpp",    { 1,  29871,    445,    338,  29871,    243,    162,    169,    156,  29889,   8223, }, },
        {"w048 7tuijk dsdfhu",  { 1,    281,  29900,  29946,  29947,  29871,  29955,   9161,  13535,  18031,   2176,   6905, }, },
        {"нещо на Български",   { 1,   1538,   4851,    665,   1386,  29713,   1305, }, },
    };
    return _k_tests;
};

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <vocab-file>\n", argv[0]);
        return 1;
    }

    const std::string fname = argv[1];

    fprintf(stderr, "%s : reading vocab from: '%s'\n", __func__, fname.c_str());

    llama_model * model;
    llama_context * ctx;

    llama_backend_init(false);

    // load the vocab
    {
        auto lparams = llama_context_default_params();

        lparams.vocab_only = true;

        model = llama_load_model_from_file(fname.c_str(), lparams);

        if (model == NULL) {
            fprintf(stderr, "%s: error: failed to load vocab '%s'\n", __func__, fname.c_str());
            return 1;
        }

        ctx = llama_new_context_with_model(model, lparams);

        if (ctx == NULL) {
            fprintf(stderr, "%s: error: failed to load vocab '%s'\n", __func__, fname.c_str());
            llama_free_model(model);
            return 1;
        }
    }

    const int n_vocab = llama_n_vocab(ctx);

    if (n_vocab != 32000) {
        fprintf(stderr, "%s : expected 32000 tokens, got %d\n", __func__, n_vocab);
        llama_free_model(model);
        llama_free(ctx);
        return 2;
    }

    for (const auto & test_kv : k_tests()) {
        std::vector<llama_token> res(test_kv.first.size());
        const int n = llama_tokenize(ctx, escape_whitespace(test_kv.first.c_str()).c_str(), res.data(), int(res.size()), true);
        fprintf(stderr, "%s : '%s' tokenized to '%s'\n", 
            __func__, test_kv.first.c_str(), unescape_whitespace(ctx, res.data(), n).c_str());
        res.resize(n);

        bool correct = res.size() == test_kv.second.size();

        for (int i = 0; i < (int) res.size() && correct; ++i) {
            if (res[i] != test_kv.second[i]) {
                correct = false;
            }
        }

        if (!correct) {
            fprintf(stderr, "%s : failed test: '%s'\n", __func__, test_kv.first.c_str());
            fprintf(stderr, "%s : expected tokens: ", __func__);
            for (const auto & t : test_kv.second) {
                fprintf(stderr, "%6d, ", t);
            }
            fprintf(stderr, "\n");
            fprintf(stderr, "%s : got tokens:      ", __func__);
            for (const auto & t : res) {
                fprintf(stderr, "%6d, ", t);
            }
            fprintf(stderr, "\n");

            llama_free_model(model);
            llama_free(ctx);
            return 3;
        }
    }

    llama_free_model(model);
    llama_free(ctx);

    llama_backend_free();

    return 0;
}
