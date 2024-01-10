#include "common.h"
#include "llama.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sstream>
#include <thread>
#include <mutex>
#include <vector>
#include <fstream>
#include <unordered_map>
#include <algorithm>

#if defined(_MSC_VER)
#pragma warning(disable: 4244 4267) // possible loss of data
#endif

struct Stats {
    std::vector<float> values;
    int ncall = 0;
};

static std::unordered_map<std::string, Stats>& ik_get_stats() {
    static std::unordered_map<std::string, Stats> g_stats;
    return g_stats;
}

struct StatParams {
    std::string ofile = "imatrix.dat";
    int         n_output_frequency = 10;
    bool        collect_output_weight = false;
};

class IMatrixCollector {
public:
    IMatrixCollector() = default;
    void set_parameters(StatParams&& params) { m_params = std::move(params); }
    void collect_imatrix(const struct ggml_tensor * src0, const struct ggml_tensor * src1);
    void save_imatrix() const;
private:
    std::unordered_map<std::string, Stats> m_stats;
    StatParams                             m_params;
    std::mutex                             m_mutex;
    int                                    m_last_call = 0;
};

void IMatrixCollector::collect_imatrix(const struct ggml_tensor * src0, const struct ggml_tensor * src1) {
    if (src1->ne[1] < 16 || src1->type != GGML_TYPE_F32) return;
    if (!(strncmp(src0->name, "blk.", 4) == 0 || (m_params.collect_output_weight && strcmp(src0->name, "output.weight") == 0))) return;
    //if (strncmp(src0->name, "blk.", 4) != 0) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto& e = m_stats[src0->name];
    if (e.values.empty()) {
        e.values.resize(src1->ne[0], 0);
    }
    else if (e.values.size() != (size_t)src1->ne[0]) {
        fprintf(stderr, "Oops: inconsistent size for %s (%d vs %d)\n", src0->name, (int)e.values.size(), (int)src1->ne[0]);
        exit(1); //GGML_ASSERT(false);
    }
    ++e.ncall;
    printf("%s[%d]: %s, %d x %d, %d\n",__func__,m_last_call,src0->name,(int)src1->ne[0],(int)src1->ne[1],(int)src1->type);
    for (int row = 0; row < (int)src1->ne[1]; ++row) {
        const float * x = (const float *)src1->data + row * src1->ne[0];
        for (int j = 0; j < (int)src1->ne[0]; ++j) {
            e.values[j] += x[j]*x[j];
        }
    }
    if (e.ncall > m_last_call) {
        m_last_call = e.ncall;
        if (m_last_call % m_params.n_output_frequency == 0) {
            save_imatrix();
        }
    }
}

void IMatrixCollector::save_imatrix() const {
    const char * fname = m_params.ofile.empty() ? "imatrix.dat" : m_params.ofile.c_str();
    std::ofstream out(fname, std::ios::binary);
    int n_entries = m_stats.size();
    out.write((const char*)&n_entries, sizeof(n_entries));
    for (auto& p : m_stats) {
        int len = p.first.size();
        out.write((const char*)&len, sizeof(len));
        out.write(p.first.c_str(), len);
        out.write((const char*)&p.second.ncall, sizeof(p.second.ncall));
        int nval = p.second.values.size();
        out.write((const char*)&nval, sizeof(nval));
        if (nval > 0) out.write((const char*)p.second.values.data(), nval*sizeof(float));
    }
    fprintf(stderr, "%s: stored collected data after %d calls in %s\n",__func__,m_last_call,fname);
}

static IMatrixCollector g_collector;

//static void ik_save_statistics(const char * fname, const std::unordered_map<std::string, Stats>& stats, int ncall) {
//    std::ofstream out(fname, std::ios::binary);
//    int n_entries = stats.size();
//    out.write((const char*)&n_entries, sizeof(n_entries));
//    for (auto& p : stats) {
//        int len = p.first.size();
//        out.write((const char*)&len, sizeof(len));
//        out.write(p.first.c_str(), len);
//        out.write((const char*)&p.second.ncall, sizeof(p.second.ncall));
//        int nval = p.second.values.size();
//        out.write((const char*)&nval, sizeof(nval));
//        if (nval > 0) out.write((const char*)p.second.values.data(), nval*sizeof(float));
//    }
//    fprintf(stderr, "%s: stored collected data after %d calls in %s\n",__func__,ncall,fname);
//}

static void ik_collect_imatrix(const struct ggml_tensor * src0, const struct ggml_tensor * src1) {
    g_collector.collect_imatrix(src0, src1);
    //static int last_call = 0;
    //static std::mutex mutex;
    //if (src1->ne[1] < 16 || src1->type != GGML_TYPE_F32) return;
    ////if (strncmp(src0->name, "blk.", 4) != 0 && strcmp(src0->name, "output.weight") != 0) return;
    //if (strncmp(src0->name, "blk.", 4) != 0) return;
    //std::lock_guard<std::mutex> lock(mutex);
    //auto& g_stats = ik_get_stats();
    //auto& e = g_stats[src0->name];
    //if (e.values.empty()) {
    //    e.values.resize(src1->ne[0], 0);
    //}
    //else if (e.values.size() != (size_t)src1->ne[0]) {
    //    fprintf(stderr, "Oops: inconsistent size for %s (%d vs %d)\n", src0->name, (int)e.values.size(), (int)src1->ne[0]);
    //    exit(1); //GGML_ASSERT(false);
    //}
    //++e.ncall;
    //printf("%s[%d]: %s, %d x %d, %d\n",__func__,last_call,src0->name,(int)src1->ne[0],(int)src1->ne[1],(int)src1->type);
    //for (int row = 0; row < (int)src1->ne[1]; ++row) {
    //    const float * x = (const float *)src1->data + row * src1->ne[0];
    //    for (int j = 0; j < (int)src1->ne[0]; ++j) {
    //        e.values[j] += x[j]*x[j];
    //    }
    //}
    //if (e.ncall > last_call) {
    //    last_call = e.ncall;
    //    if (last_call % 10 == 0) {
    //        ik_save_statistics("stats.dat", g_stats, last_call);
    //    }
    //}
}


struct results_log_softmax {
    double log_softmax;
    float  logit;
    float  prob;
};

static std::vector<float> softmax(const std::vector<float>& logits) {
    std::vector<float> probs(logits.size());
    float max_logit = logits[0];
    for (float v : logits) {
        max_logit = std::max(max_logit, v);
    }
    double sum_exp = 0.0;
    for (size_t i = 0; i < logits.size(); i++) {
        // Subtract the maximum logit value from the current logit value for numerical stability
        const float logit = logits[i] - max_logit;
        const float exp_logit = expf(logit);
        sum_exp += exp_logit;
        probs[i] = exp_logit;
    }
    for (size_t i = 0; i < probs.size(); i++) {
        probs[i] /= sum_exp;
    }
    return probs;
}

static results_log_softmax log_softmax(int n_vocab, const float * logits, int tok) {
    float max_logit = logits[0];
    for (int i = 1; i < n_vocab; ++i) {
        max_logit = std::max(max_logit, logits[i]);
    }
    double sum_exp = 0.0;
    for (int i = 0; i < n_vocab; ++i) {
        sum_exp += expf(logits[i] - max_logit);
    }
    return {logits[tok] - max_logit - log(sum_exp), logits[tok], expf(logits[tok] - max_logit) / (float) sum_exp};
}

static void process_logits(
    int n_vocab, const float * logits, const int * tokens, int n_token, std::vector<std::thread> & workers,
    double & nll, double & nll2, float * logit_history, float * prob_history
) {
    std::mutex mutex;
    int counter = 0;
    auto compute = [&mutex, &counter, &nll, &nll2, logit_history, prob_history, n_vocab, logits, tokens, n_token] () {
        double local_nll  = 0;
        double local_nll2 = 0;
        while (true) {
            std::unique_lock<std::mutex> lock(mutex);
            int i = counter++;
            if (i >= n_token) {
                nll += local_nll; nll2 += local_nll2;
                break;
            }
            lock.unlock();
            const results_log_softmax results = log_softmax(n_vocab, logits + i*n_vocab, tokens[i+1]);
            const double v = -results.log_softmax;
            local_nll += v;
            local_nll2 += v*v;

            logit_history[i] = results.logit;
            prob_history[i]  = results.prob;
        }
    };
    for (auto & w : workers) {
        w = std::thread(compute);
    }
    compute();
    for (auto & w : workers) {
        w.join();
    }
}

static bool compute_imatrix(llama_context * ctx, const gpt_params & params) {

    const bool add_bos = llama_should_add_bos_token(llama_get_model(ctx));
    const int n_ctx = llama_n_ctx(ctx);

    auto tim1 = std::chrono::high_resolution_clock::now();
    fprintf(stderr, "%s: tokenizing the input ..\n", __func__);

    std::vector<llama_token> tokens = ::llama_tokenize(ctx, params.prompt, add_bos);

    auto tim2 = std::chrono::high_resolution_clock::now();
    fprintf(stderr, "%s: tokenization took %g ms\n",__func__,1e-3*std::chrono::duration_cast<std::chrono::microseconds>(tim2-tim1).count());

    if (int(tokens.size()) < 2*n_ctx) {
        fprintf(stderr, "%s: you need at least %d tokens for a context of %d tokens\n",__func__,2*n_ctx,
                n_ctx);
        fprintf(stderr, "%s: the data file you provided tokenizes to only %zu tokens\n",__func__,tokens.size());
        return false;
    }

    std::vector<float> logit_history;
    logit_history.resize(tokens.size());

    std::vector<float> prob_history;
    prob_history.resize(tokens.size());

    const int n_chunk_max = tokens.size() / n_ctx;

    const int n_chunk = params.n_chunks < 0 ? n_chunk_max : std::min(params.n_chunks, n_chunk_max);
    const int n_vocab = llama_n_vocab(llama_get_model(ctx));
    const int n_batch = params.n_batch;

    int count = 0;
    double nll = 0.0;
    double nll2 = 0.0;

    fprintf(stderr, "%s: computing over %d chunks with batch_size %d\n", __func__, n_chunk, n_batch);

    std::vector<std::thread> workers(std::thread::hardware_concurrency() - 1);

    for (int i = 0; i < n_chunk; ++i) {
        const int start =     i * n_ctx;
        const int end   = start + n_ctx;

        const int num_batches = (n_ctx + n_batch - 1) / n_batch;

        std::vector<float> logits;

        const auto t_start = std::chrono::high_resolution_clock::now();

        // clear the KV cache
        llama_kv_cache_clear(ctx);

        for (int j = 0; j < num_batches; ++j) {
            const int batch_start = start + j * n_batch;
            const int batch_size  = std::min(end - batch_start, n_batch);

            // save original token and restore it after eval
            const auto token_org = tokens[batch_start];

            // add BOS token for the first batch of each chunk
            if (add_bos && j == 0) {
                tokens[batch_start] = llama_token_bos(llama_get_model(ctx));
            }

            if (llama_decode(ctx, llama_batch_get_one(tokens.data() + batch_start, batch_size, j * n_batch, 0))) {
                fprintf(stderr, "%s : failed to eval\n", __func__);
                return false;
            }

            // restore the original token in case it was set to BOS
            tokens[batch_start] = token_org;

            const auto * batch_logits = llama_get_logits(ctx);
            logits.insert(logits.end(), batch_logits, batch_logits + batch_size * n_vocab);
        }

        const auto t_end = std::chrono::high_resolution_clock::now();

        if (i == 0) {
            const float t_total = std::chrono::duration<float>(t_end - t_start).count();
            fprintf(stderr, "%s: %.2f seconds per pass - ETA ", __func__, t_total);
            int total_seconds = (int)(t_total * n_chunk);
            if (total_seconds >= 60*60) {
                fprintf(stderr, "%d hours ", total_seconds / (60*60));
                total_seconds = total_seconds % (60*60);
            }
            fprintf(stderr, "%.2f minutes\n", total_seconds / 60.0);
        }

        const int first = n_ctx/2;
        process_logits(n_vocab, logits.data() + first*n_vocab, tokens.data() + start + first, n_ctx - 1 - first,
                       workers, nll, nll2, logit_history.data() + start + first, prob_history.data() + start + first);
        count += n_ctx - first - 1;

        printf("[%d]%.4lf,", i + 1, std::exp(nll / count));
        fflush(stdout);
    }
    printf("\n");

    nll2 /= count;
    nll /= count;
    const double ppl = exp(nll);
    nll2 -= nll * nll;
    if (nll2 > 0) {
        nll2 = sqrt(nll2/(count-1));
        printf("Final estimate: PPL = %.4lf +/- %.5lf\n", ppl, nll2*ppl);
    } else {
        printf("Unexpected negative standard deviation of log(prob)\n");
    }

    return true;
}

int main(int argc, char ** argv) {

    StatParams sparams;
    std::vector<char*> args;
    args.push_back(argv[0]);
    int iarg = 1;
    for (; iarg < argc-1; ++iarg) {
        std::string arg{argv[iarg]};
        if (arg == "-o" || arg == "--output-file") {
            sparams.ofile = argv[++iarg];
        }
        else if (arg == "-ofreq" || arg == "--output-frequency") {
            sparams.n_output_frequency = std::stoi(argv[++iarg]);
        }
        else if (arg == "-ow" || arg == "--output-weight") {
            sparams.collect_output_weight = std::stoi(argv[++iarg]);
        } else {
            args.push_back(argv[iarg]);
        }
    }
    if (iarg < argc) {
        args.push_back(argv[iarg]);
    }

    gpt_params params;
    params.n_batch = 512;
    if (!gpt_params_parse(args.size(), args.data(), params)) {
        return 1;
    }

    g_collector.set_parameters(std::move(sparams));

    ggml_set_imatrix_collection(ik_collect_imatrix);
    ggml_set_imatrix_collection(ik_collect_imatrix);

    params.logits_all = true;
    params.n_batch = std::min(params.n_batch, params.n_ctx);

    print_build_info();

    if (params.seed == LLAMA_DEFAULT_SEED) {
        params.seed = time(NULL);
    }

    fprintf(stderr, "%s: seed  = %u\n", __func__, params.seed);

    std::mt19937 rng(params.seed);
    if (params.random_prompt) {
        params.prompt = gpt_random_prompt(rng);
    }

    llama_backend_init(params.numa);

    llama_model * model;
    llama_context * ctx;

    // load the model and apply lora adapter, if any
    std::tie(model, ctx) = llama_init_from_gpt_params(params);
    if (model == NULL) {
        fprintf(stderr, "%s: error: unable to load model\n", __func__);
        return 1;
    }

    const int n_ctx_train = llama_n_ctx_train(model);
    if (params.n_ctx > n_ctx_train) {
        fprintf(stderr, "%s: warning: model was trained on only %d context tokens (%d specified)\n",
                __func__, n_ctx_train, params.n_ctx);
    }

    // print system information
    {
        fprintf(stderr, "\n");
        fprintf(stderr, "%s\n", get_system_info(params).c_str());
    }

    bool OK = compute_imatrix(ctx, params);
    if (!OK) {
        return 1;
    }

    g_collector.save_imatrix();
    //auto& stats = ik_get_stats();
    //int ncall = 0;
    //for (auto& s : stats) {
    //    ncall = std::max(ncall, s.second.ncall);
    //}
    //ik_save_statistics(sparams.ofile.c_str(), stats, ncall);

    llama_print_timings(ctx);

    llama_free(ctx);
    llama_free_model(model);

    llama_backend_free();

    return 0;
}
