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
  -sqlite, --sqlite    Build with SQLite database support. Uses build-sqlite by default.
  -postgres, --postgres
                       Build with PostgreSQL database support. Uses build-postgres by default.
  -db, --db            Build with SQLite and PostgreSQL database support. Uses build-db by default.
  -no-sqlite, --no-sqlite
                       Build without SQLite database support unless -db is used later.
  -no-postgres, --no-postgres
                       Build without PostgreSQL database support unless -db is used later.
  -debug, --debug      Build Debug. This is the default.
  -release, --release  Build Release.
  -b, --build-dir DIR  Use a custom build directory.
  -j, --jobs N         Build with N parallel jobs.
  -clean, --clean      Remove the selected build directory before configuring.
  -h, --help           Show this help.

Examples:
  ./build.sh
  ./build.sh -crypto
  ./build.sh -sqlite
  ./build.sh -db
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
sqlite=0
postgres=0
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
    -sqlite|--sqlite)
      sqlite=1
      ;;
    -no-sqlite|--no-sqlite)
      sqlite=0
      ;;
    -postgres|--postgres)
      postgres=1
      ;;
    -no-postgres|--no-postgres)
      postgres=0
      ;;
    -db|--db)
      sqlite=1
      postgres=1
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
    -DSYPHAX_WEB_ENABLE_SQLITE=ON|-DSYPHAX_WEB_ENABLE_SQLITE=1|-DSYPHAX_WEB_ENABLE_SQLITE=TRUE)
      sqlite=1
      ;;
    -DSYPHAX_WEB_ENABLE_SQLITE=OFF|-DSYPHAX_WEB_ENABLE_SQLITE=0|-DSYPHAX_WEB_ENABLE_SQLITE=FALSE)
      sqlite=0
      ;;
    -DSYPHAX_WEB_ENABLE_POSTGRES=ON|-DSYPHAX_WEB_ENABLE_POSTGRES=1|-DSYPHAX_WEB_ENABLE_POSTGRES=TRUE)
      postgres=1
      ;;
    -DSYPHAX_WEB_ENABLE_POSTGRES=OFF|-DSYPHAX_WEB_ENABLE_POSTGRES=0|-DSYPHAX_WEB_ENABLE_POSTGRES=FALSE)
      postgres=0
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
  elif [ "${sqlite}" -eq 1 ] && [ "${postgres}" -eq 1 ]; then
    build_dir="build-db"
  elif [ "${sqlite}" -eq 1 ]; then
    build_dir="build-sqlite"
  elif [ "${postgres}" -eq 1 ]; then
    build_dir="build-postgres"
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
if [ "${sqlite}" -eq 1 ]; then
  sqlite_value="ON"
else
  sqlite_value="OFF"
fi
if [ "${postgres}" -eq 1 ]; then
  postgres_value="ON"
else
  postgres_value="OFF"
fi

mkdir -p bin
bin_marker="bin/.syphax_web_active_build"
bin_config="build_dir=${build_dir};build_type=${build_type};tls=${tls_value};crypto=${crypto_value};sqlite=${sqlite_value};postgres=${postgres_value};cmake_args=${cmake_args[*]}"
previous_bin_config=""
if [ -f "${bin_marker}" ]; then
  previous_bin_config="$(cat "${bin_marker}")"
fi

if [ "${clean}" -eq 1 ] || [ "${previous_bin_config}" != "${bin_config}" ]; then
  echo "Refreshing shared bin outputs for ${build_dir} (${build_type}, TLS=${tls_value}, CRYPTO=${crypto_value}, SQLITE=${sqlite_value}, POSTGRES=${postgres_value})"
  rm -f \
    bin/01_http \
    bin/02_https \
    bin/03_static_site \
    bin/04_live_queue \
    bin/05_folder_app \
    bin/06_session_login \
    bin/07_database \
    bin/server_stress \
    bin/syphax_web_tests
fi

echo "Configuring ${build_dir} (${build_type}, TLS=${tls_value}, CRYPTO=${crypto_value}, SQLITE=${sqlite_value}, POSTGRES=${postgres_value})"
cmake -S . -B "${build_dir}" \
  -DCMAKE_BUILD_TYPE="${build_type}" \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DSYPHAX_WEB_BUILD_EXAMPLES=ON \
  -DSYPHAX_WEB_BUILD_TESTS=ON \
  -DSYPHAX_WEB_BUILD_STRESS=ON \
  -DSYPHAX_WEB_ENABLE_TLS="${tls_value}" \
  -DSYPHAX_WEB_ENABLE_CRYPTO="${crypto_value}" \
  -DSYPHAX_WEB_ENABLE_SQLITE="${sqlite_value}" \
  -DSYPHAX_WEB_ENABLE_POSTGRES="${postgres_value}" \
  "${cmake_args[@]}"

echo "Building ${build_dir} with ${parallel_jobs} job(s)"
cmake --build "${build_dir}" --parallel "${parallel_jobs}"
printf '%s\n' "${bin_config}" > "${bin_marker}"
