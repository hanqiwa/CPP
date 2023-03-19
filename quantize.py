#!/usr/bin/env python3

"""Script to execute quantization on a given model."""

import subprocess
import argparse
import sys
import os


def main():
    """Parse the command line arguments and execute the script."""

    parser = argparse.ArgumentParser(
        prog='Quantization Script',
        description='This script quantizes a model or many models.'
    )
    parser.add_argument(
        "models", nargs='+', dest='models',
        choices=('7B', '13B', '30B', '65B'),
        help='The models to quantize.'
    )
    parser.add_argument(
        '-r', '--remove-16', action='store_true', dest='remove_f16',
        help='Remove the f16 model after quantizing it.'
    )
    parser.add_argument(
        '-m', '--models-path', dest='models_path',
        default=os.path.join(os.getcwd(), "models"),
        help='Specify the directory where the models are located.'
    )
    parser.add_argument(
        '-q', '--quantize-script-path', dest='quantize_script_path',
        default=os.getcwd(),
        help='Specify the path to the "quantize" script.'
    )
    # TODO: Revise this code
    # parser.add_argument(
    #     '-t', '--threads', dest='threads', type='int',
    #     default=os.cpu_count(),
    #     help='Specify the number of threads to use to quantize many models at '
    #     'once. Defaults to os.cpu_count().'
    # )

    args = parser.parse_args()

    if not os.path.isfile(args.quantize_script_path):
        print(
            'The "quantize" script was not found in the current location.\n'
            "If you want to use it from another location, set the "
            "--quantize-script-path argument from the command line."
        )
        sys.exit(1)

    for model in args.models:
        f16_model_path = os.path.join(
            args.models_path, model, "ggml-model-f16.bin"
        )

        if not os.path.isfile(f16_model_path):
            print(
                "The f16 model (ggml-model-f16.bin) was not found in "
                f"models/{model}. If you want to use it from another location,"
                " set the --models-path argument from the command line."
            )
            sys.exit(1)

        __run_quantize_script(args.quantize_script_path, f16_model_path)

        if args.remove_f16:
            os.remove(f16_model_path)


# This was extracted to a top-level function for parallelization, if
# implemented. See https://github.com/ggerganov/llama.cpp/pull/222/commits/f8db3d6cd91bf1a1342db9d29e3092bc12dd783c#r1140496406
def __run_quantize_script(script_path, f16_model_path):
    """Run the quantize script specifying the path to it and the path to the
    f16 model to quantize.
    """

    new_quantized_model_path = f16_model_path.replace("16", "q4_0")
    subprocess.run(
        [script_path, f16_model_path, new_quantized_model_path, "2"],
        shell=True, check=True
    )


if __name__ == "__main__":
    try:
        main()

    except subprocess.CalledProcessError:
        print("An error ocurred while trying to quantize the models.")
        sys.exit(1)

    except KeyboardInterrupt:
        sys.exit(0)
