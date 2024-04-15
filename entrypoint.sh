#!/bin/bash
#
# Entrypoint script to map environment variables into command line arguments.
#
# Three kinds of arguments are supported:
#
# 1. Option and value EG "--port 8888"
#
# Configure these by adding keys to the ARG_MAPPING dictionary in the code. See
# the comments there for details.
# 
# Arguments and values will be space separated and values will be single quoted
# in the resulting command EG "--port '8888'". Any unset or empty string 
# environment varables do not result in any command line argument being 
# specified. 
#
# 2. Flags EG "--verbose"
#
# Configure these by adding keys to the ARG_MAPPING dictionary in the code the
# same as above. But use them by setting the environment variable's value to the
# magic Python-inspired value "ACTION_STORE_TRUE".
#
# 3. Subcommands or bare values EG "docker run"
#
# Configure one environment variable name in the ARG_MAPPING dictionary in the
# code the same as above by specifying it with the command line argument magic 
# value of "&". This environment variable's value will be added to the very
# end of the command with no quotes.
#
# Any additional arguments passed to this script, except "--debug", will be 
# passed to the command. Passing "--debug" will cause this script to enter
# debugging mode and print out information without running the final command.
#
# This requires BASH VERSION 4 OR GREATER. Beware Alpine and other busybox or
# ash based distros.
#
# Example usage in a Dockerfile:
#
#   COPY ./entrypoint.sh .
#   # Set any default values here, if it's appropriate to build them into the
#   # container.
#   ENV SAMPLE_VARIABLE=foo
#   ENV LESS_USED_OPTION=bar
#
#   # Uncomment to build container in debugging entrypoint.sh mode.
#   #CMD = ["--debug"]
#   ENTRYPOINT ["./entrypoint.sh"]
#
# Example usage in a compose.yml file that runs the above Dockerfile:
#
#   services:
#     my_service_name:
#       image: dockerimagename
#       environment:
#         SAMPLE_VARIABLE: foo_override
#         SOME_OTHER_VARIABLE: not_foo
#         LESS_USED_OPTION: '' # value is set to empty string which entrypoint.sh
#                              # will interpret as unset
#
# Examples running the container from the command line:
#
#  # Run container in debugging mode, outputing the command that would be run
#  docker run dockerimagename --debug
#
#  # Run container overriding SAMPLE_VARIABLE to a new value
#  docker run -e SAMPLE_VARIABLE='foo_override' dockerimagename
#
#  # Run container with any built-in default variables, but additional command
#  # line arguments
#  docker run dockerimagename -v --rarely-used-option bar
#

set -euo pipefail

declare -A ARG_MAPPING

# Set program name to run here.
COMMAND_LINE="/slinktool"

# Add arguments specific to your program here in the form:
# ARGMAPPING["command line arg including dashes"]="environment variable name"
ARG_MAPPING["-V"]="VERSION"
ARG_MAPPING["-h"]="HELP"
ARG_MAPPING["-H"]="EXTENDED_HELP"
ARG_MAPPING["-vv"]="VERBOSE"
ARG_MAPPING["-P"]="PING"
ARG_MAPPING["-pp"]="PRINT_RECORD_DETAILS"
ARG_MAPPING["-u"]="PRINT_RECORD_DATA"
ARG_MAPPING["-nd"]="NETWORK_RECONNECT_DELAY"
ARG_MAPPING["-nt"]="NETWORK_TIMEOUT"
ARG_MAPPING["-k"]="KEEPALIVE"
ARG_MAPPING["-x"]="STATEFILE"
ARG_MAPPING["-d"]="DIALUP"
ARG_MAPPING["-b"]="BATCH"
ARG_MAPPING["-o"]="DUMPFILE"
ARG_MAPPING["-A"]="DIRECTORY_FORMAT"
ARG_MAPPING["-SDS"]="SDS_DIRECTORY"
ARG_MAPPING["-BUD"]="BUD_DIRECTORY"
ARG_MAPPING["-s"]="SELECTORS"
ARG_MAPPING["-I"]="STREAM_LIST_FILE"
ARG_MAPPING["-S"]="STREAM_LIST"
ARG_MAPPING["-tw"]="TIME_WINDOW"
ARG_MAPPING["-i"]="INFO_REQUEST"
ARG_MAPPING["-I"]="INFO_ID"
ARG_MAPPING["-L"]="INFO_STATION_LIST"
ARG_MAPPING["-Q"]="INFO_STREAM_LIST"
ARG_MAPPING["-G"]="INFO_GAP_LIST"
ARG_MAPPING["-C"]="INFO_CONNECTION_LIST"
ARG_MAPPING["&"]="HOST_AND_PORT"

# Process any additional command line args, eating "--debug" as an argument to
# this script.
DEBUGGING=
for arg
do
    shift
    if [[ "$arg" == "--debug" ]] ; then
        DEBUGGING='true'
    else
        set -- "$@" "$arg"
    fi
done

if [[ $DEBUGGING ]] ; then
    echo "Argument --debug detected, entrypoint.sh entering debugging mode. No command will be run."
fi

# Build command line arguments from environment variables
SUFFIX_ARGUMENT=
for cl_arg in "${!ARG_MAPPING[@]}";
do
    env_var="${ARG_MAPPING[$cl_arg]}"
    if [[ ! -z ${!env_var+x} ]] ; then
        if [[ "${!env_var}" == "ACTION_STORE_TRUE" ]] ; then
            # Flag option
            COMMAND_LINE="${COMMAND_LINE} ${cl_arg}"
        elif [[ "${cl_arg}" == "&" ]] ; then
            # Subcommand option
            SUFFIX_ARGUMENT=${!env_var}
        else
            # Option and value
            COMMAND_LINE="${COMMAND_LINE} ${cl_arg} ${!env_var}"
        fi
    elif [[ $DEBUGGING ]] ; then
        echo "Argument $cl_arg not passed because variable $env_var is unset"
    fi
done

# Run command
if [[ $DEBUGGING ]] ; then
    echo "Additional arguments passed to entrypoint.sh: $@"
    echo "Final command to be run: $COMMAND_LINE $@ $SUFFIX_ARGUMENT"
else
    exec $COMMAND_LINE $@ $SUFFIX_ARGUMENT
fi
