Feature: llama.cpp server

  Background: Server startup
    Given a server listening on localhost:8080 with 2 slots and 42 as seed
    Then  the server is starting
    Then  the server is healthy

  @llama.cpp
  Scenario: Health
    When the server is healthy
    Then the server is ready
    And  all slots are idle

  @llama.cpp
  Scenario Outline: Completion
    Given a <prompt> completion request with maximum <n_predict> tokens
    Then  <predicted_n> tokens are predicted

    Examples: Prompts
      | prompt                           | n_predict | predicted_n |
      | I believe the meaning of life is | 128       | 128         |
      | Write a joke about AI            | 512       | 512         |

  @llama.cpp
  Scenario Outline: OAI Compatibility
    Given a system prompt <system_prompt>
    And   a user prompt <user_prompt>
    And   a model <model>
    And   <max_tokens> max tokens to predict
    And   streaming is <enable_streaming>
    Given an OAI compatible chat completions request
    Then  <predicted_n> tokens are predicted

    Examples: Prompts
      | model        | system_prompt               | user_prompt                          | max_tokens | enable_streaming | predicted_n |
      | llama-2      | You are ChatGPT.            | Say hello.                           | 64         | false            | 64          |
      | codellama70b | You are a coding assistant. | Write the fibonacci function in c++. | 512        | true             | 512         |

  @llama.cpp
  Scenario: Multi users
    Given a prompt:
      """
      Write a very long story about AI.
      """
    And a prompt:
      """
      Write another very long music lyrics.
      """
    And 32 max tokens to predict
    Given concurrent completion requests
    Then the server is busy
    And  all slots are busy
    Then the server is idle
    And  all slots are idle
    Then all prompts are predicted

  @llama.cpp
  Scenario: Multi users OAI Compatibility
    Given a system prompt "You are an AI assistant."
    And a model tinyllama-2
    Given a prompt:
      """
      Write a very long story about AI.
      """
    And a prompt:
      """
      Write another very long music lyrics.
      """
    And 32 max tokens to predict
    And streaming is enabled
    Given concurrent OAI completions requests
    Then the server is busy
    And  all slots are busy
    Then the server is idle
    And  all slots are idle
    Then all prompts are predicted

  # FIXME: #3969 infinite loop on the CI, not locally, if n_prompt * n_predict > kv_size
  @bug
  Scenario: Multi users with total number of tokens to predict exceeds the KV Cache size
    Given a prompt:
      """
      Write a very long story about AI.
      """
    And a prompt:
      """
      Write another very long music lyrics.
      """
    And a prompt:
      """
      Write a very long poem.
      """
    And 1024 max tokens to predict
    Given concurrent completion requests
    Then the server is busy
    And  all slots are busy
    Then the server is idle
    And  all slots are idle
    Then all prompts are predicted


  @llama.cpp
  Scenario: Embedding
    When embeddings are computed for:
    """
    What is the capital of Bulgaria ?
    """
    Then embeddings are generated


  @llama.cpp
  Scenario: OAI Embeddings compatibility
    Given a model tinyllama-2
    When an OAI compatible embeddings computation request for:
    """
    What is the capital of Spain ?
    """
    Then embeddings are generated


  @llama.cpp
  Scenario: Tokenize / Detokenize
    When tokenizing:
    """
    What is the capital of France ?
    """
    Then tokens can be detokenize

