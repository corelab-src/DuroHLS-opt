all: configure

configure :
		mkdir llvm-corelab-install; cd llvm-corelab-install; \
		../llvm-corelab/configure --with-llvmsrc=${LLVM_SRC_ROOT} -with-llvmobj=${LLVM_OBJ_DIR} --prefix=${PWD} --enable-optimized --with-optimize-option=-O3; \
		cd -; fi

