#!/usr/bin/env bash
set -eu

usage() {
  cat <<'EOF'
Usage: ./build.sh [options] [cmake -D options]

Options:
  -tls, --tls          Build with OpenSSL TLS support. Uses build-tls by default.
  -no-tls, --no-tls    Build without TLS. Uses build by default.
  -crypto, --crypto    Build with OpenSSL encrypted token support. Uses build-crypto by default.
  -no-crypto, --no-crypto
                       Build without encrypted token support unless TLS is enabled.
  -debug, --debug      Build Debug. This is the default.
  -release, --release  Build Release.
  -b, --build-dir DIR  Use a custom build directory.
  -j, --jobs N         Build with N parallel jobs.
  -clean, --clean      Remove the selected build directory before configuring.
  -h, --help           Show this help.

Examples:
  ./build.sh
  ./build.sh -crypto
  ./build.sh -tls
  ./build.sh -tls -release
  ./build.sh -DSYPHAX_WEB_ENABLE_TLS=ON
EOF
}

parallel_jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || true)"
if [ -z "${parallel_jobs}" ] || [ "${parallel_jobs}" -lt 1 ] 2>/dev/null; then
  parallel_jobs=1
fi

build_type="Debug"
build_dir=""
clean=0
tls=0
crypto=0
cmake_args=()

while [ "$#" -gt 0 ]; do
  case "$1" in
    -h|--help)
      usage
      exit 0
      ;;
    -tls|--tls)
      tls=1
      ;;
    -no-tls|--no-tls)
      tls=0
      ;;
    -crypto|--crypto)
      crypto=1
      ;;
    -no-crypto|--no-crypto)
      crypto=0
      ;;
    -debug|--debug)
      build_type="Debug"
      ;;
    -release|--release)
      build_type="Release"
      ;;
    -b|--build-dir)
      shift
      if [ "$#" -eq 0 ]; then
        echo "error: missing build directory after -b" >&2
        exit 2
      fi
      build_dir="$1"
      ;;
    -j|--jobs)
      shift
      if [ "$#" -eq 0 ]; then
        echo "error: missing job count after -j" >&2
        exit 2
      fi
      parallel_jobs="$1"
      ;;
    -clean|--clean)
      clean=1
      ;;
    -DSYPHAX_WEB_ENABLE_TLS=ON|-DSYPHAX_WEB_ENABLE_TLS=1|-DSYPHAX_WEB_ENABLE_TLS=TRUE)
      tls=1
      ;;
    -DSYPHAX_WEB_ENABLE_TLS=OFF|-DSYPHAX_WEB_ENABLE_TLS=0|-DSYPHAX_WEB_ENABLE_TLS=FALSE)
      tls=0
      ;;
    -DSYPHAX_WEB_ENABLE_CRYPTO=ON|-DSYPHAX_WEB_ENABLE_CRYPTO=1|-DSYPHAX_WEB_ENABLE_CRYPTO=TRUE)
      crypto=1
      ;;
    -DSYPHAX_WEB_ENABLE_CRYPTO=OFF|-DSYPHAX_WEB_ENABLE_CRYPTO=0|-DSYPHAX_WEB_ENABLE_CRYPTO=FALSE)
      crypto=0
      ;;
    -D*)
      cmake_args+=("$1")
      ;;
    --)
      shift
      while [ "$#" -gt 0 ]; do
        cmake_args+=("$1")
        shift
      done
      break
      ;;
    *)
      echo "error: unknown option: $1" >&2
      echo "Run ./build.sh --help for usage." >&2
      exit 2
      ;;
  esac
  shift
done

if [ "${tls}" -eq 1 ]; then
  crypto=1
fi

if [ -z "${build_dir}" ]; then
  if [ "${tls}" -eq 1 ]; then
    build_dir="build-tls"
  elif [ "${crypto}" -eq 1 ]; then
    build_dir="build-crypto"
  else
    build_dir="build"
  fi
fi

case "${parallel_jobs}" in
  ''|*[!0-9]*)
    echo "error: job count must be a positive integer" >&2
    exit 2
    ;;
esac

if [ "${parallel_jobs}" -lt 1 ]; then
  echo "error: job count must be a positive integer" >&2
  exit 2
fi

if [ ! -f "lib/syphax/s_array.h" ]; then
  git submodule update --init --recursive
fi

if [ "${clean}" -eq 1 ]; then
  rm -rf "${build_dir}"
fi

if [ "${tls}" -eq 1 ]; then
  tls_value="ON"
else
  tls_value="OFF"
fi
if [ "${crypto}" -eq 1 ]; then
  crypto_value="ON"
else
  crypto_value="OFF"
fi

mkdir -p bin
bin_marker="bin/.syphax_web_active_build"
bin_config="build_dir=${build_dir};build_type=${build_type};tls=${tls_value};crypto=${crypto_value};cmake_args=${cmake_args[*]}"
previous_bin_config=""
if [ -f "${bin_marker}" ]; then
  previous_bin_config="$(cat "${bin_marker}")"
fi

if [ "${clean}" -eq 1 ] || [ "${previous_bin_config}" != "${bin_config}" ]; then
  echo "Refreshing shared bin outputs for ${build_dir} (${build_type}, TLS=${tls_value}, CRYPTO=${crypto_value})"
  rm -f \
    bin/01_http \
    bin/02_https \
    bin/03_static_site \
    bin/04_live_queue \
    bin/05_folder_app \
    bin/06_session_login \
    bin/syphax_web_tests
fi

echo "Configuring ${build_dir} (${build_type}, TLS=${tls_value}, CRYPTO=${crypto_value})"
cmake -S . -B "${build_dir}" \
  -DCMAKE_BUILD_TYPE="${build_type}" \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DSYPHAX_WEB_BUILD_EXAMPLES=ON \
  -DSYPHAX_WEB_BUILD_TESTS=ON \
  -DSYPHAX_WEB_ENABLE_TLS="${tls_value}" \
  -DSYPHAX_WEB_ENABLE_CRYPTO="${crypto_value}" \
  "${cmake_args[@]}"

echo "Building ${build_dir} with ${parallel_jobs} job(s)"
cmake --build "${build_dir}" --parallel "${parallel_jobs}"
printf '%s\n' "${bin_config}" > "${bin_marker}"
