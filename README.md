## Typed Lisp

Research language targeting LLVM.

### Building from source (Debian)

Update system packages and install required dependencies.

```bash
sudo apt update
sudo apt upgrade -y
sudo apt install -y lsb-release wget software-properties-common gnupg build-essential
```

Add the LLVM repository key:

```bash
wget -O- https://apt.llvm.org/llvm-snapshot.gpg.key | sudo apt-key add -
```

Add the LLVM repository to sources:

```bash
sudo add-apt-repository "deb http://apt.llvm.org/$(lsb_release -cs)/ llvm-toolchain-$(lsb_release -cs)-16 main"
```

Update package lists with the new repository:

```bash
sudo apt update
```

Install the LLVM 16 packages:

```bash
sudo apt install -y llvm-16 llvm-16-dev llvm-16-tools clang-16 libclang-16-dev lld-16
```

Create symlinks and override local paths:

```bash
sudo ln -s /usr/bin/llvm-config-16 /usr/local/bin/llvm-config
sudo ln -s /usr/bin/clang-16 /usr/local/bin/clang
sudo ln -s /usr/bin/clang++-16 /usr/local/bin/clang++
sudo ln -s /usr/bin/llc-16 /usr/local/bin/llc
sudo ln -s /usr/bin/opt-16 /usr/local/bin/opt
```

Set up environment variables for `llvm-config`. Note that the required components to link against are only `core`, `bitwriter` and `support` (use `llvm-config --components | grep <>` to validate if components). There may be linking issues unless these flags are configured. If the build still fails, reinstall again.

```bash
echo 'export PATH="/usr/lib/llvm-16/bin:$PATH"' >> ~/.bashrc
echo 'export LLVM_CONFIG=/usr/lib/llvm-16/bin/llvm-config' >> ~/.bashrc
source ~/.bashrc
```

Verify installation.

```bash
clang --version
llvm-config --version
```

If there are errors with the repository key, use this alternative method:

```bash
wget -qO- https://apt.llvm.org/llvm-snapshot.gpg.key | sudo tee /etc/apt/trusted.gpg.d/apt.llvm.org.asc
```

Alternatively, with older `apt` versions, install using:

```bash
sudo apt install -y clang-16 lldb-16 lld-16 clangd-16 clang-tidy-16 clang-format-16 llvm-16-dev
sudo apt clean
```

Check LLVM installation directory:

```bash
ls -la /usr/lib/llvm-16/
```

It should output a response like this (output on my PC after piping with `awk '{print $1, $2, $3, $4, $5, $9}'`):

```sh
total 68
drwxr-xr-x 7 root root 4096 .
drwxr-xr-x 94 root root 4096 ..
drwxr-xr-x 2 root root 12288 bin
drwxr-xr-x 4 root root 4096 build
lrwxrwxrwx 1 root root 14 cmake
drwxr-xr-x 8 root root 4096 include
drwxr-xr-x 4 root root 36864 lib
drwxr-xr-x 5 root root 4096 share
```

Typed Lisp requires the `llvm-libc` component which can be installed separately (it already includes definitions for both `libc` and `libc++`). GCC would not work and Clang may not find `glibc` headers. In particular, we need the libc++ ABI sub-component.

```bash
sudo apt install libc++-16-dev libc++abi-16-dev
find /usr -name libc++.so
find /usr -name libc++abi.so
```

Clone the LLVM project repository.

```bash
git clone https://github.com/llvm/llvm-project.git
cd llvm-project
```

Create a build directory for libc and move to it.

```bash
mkdir -p build-libc
cd build-libc
```

Configure the build with CMake and enable the libc component.

```bash
cmake -G Ninja ../llvm \
  -DLLVM_ENABLE_PROJECTS="clang;lld" \
  -DLLVM_ENABLE_RUNTIMES="libc" \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_LIBC_ENABLE_ALL_TARGETS=ON
```

Build and install the libc component:

```bash
ninja
sudo ninja install
```

Add the libraries to the system path:

```bash
echo "/usr/local/lib" | sudo tee /etc/ld.so.conf.d/llvm-libc.conf
sudo ldconfig
```

Configure build environment to use LLVM's libc:

```bash
export LIBC_INCLUDE_PATH="/usr/local/include/llvm-libc"
export LIBC_LIB_PATH="/usr/local/lib"
```

For a specific build target, use `CFLAGS` and `LDFLAGS`.

```bash
CFLAGS="-I${LIBC_INCLUDE_PATH}" LDFLAGS="-L${LIBC_LIB_PATH} -lllvmlibc"
```

Update compiler configuration to use LLVM's libc by default.

```bash
echo 'export CC="clang --stdlib=libc"' >> ~/.bashrc
echo 'export CXX="clang++ --stdlib=libc"' >> ~/.bashrc
source ~/.bashrc
```

Run a basic test to verify everything is working.

```bash
cat << EOF > libc_test.c
#include <stdio.h>
int main() { return 0; }
EOF

clang -fuse-ld=lld --rtlib=compiler-rt --stdlib=libc libc_test.c -o libc_test
./libc_test
```

The configuration options for Typed Lisp and the [build target options](https://github.com/elricmann/typed-lisp/blob/main/Makefile) are predefined. Run `make && make all` and use `./build/tlc`.

### Usage

Refer to the [source](https://github.com/elricmann/typed-lisp/blob/main/main.cc).

### EBNF grammar representation

```ebnf
Program     ::= Expression
Expression  ::= Atom | List
Atom        ::= Identifier | Literal
List        ::= '(' Expression* ')'
Identifier  ::= [a-zA-Z][a-zA-Z0-9]*
Literal     ::= Number | String | Boolean
Number      ::= [0-9]+
String      ::= '"' [^"]* '"'
Boolean     ::= "true" | "false"
```

### Type system rules

We define a subset of the type system rules for Typed Lisp. Ideally, there should be formal proofs to validate the typing rules once, the current implementation lacks a few rules.

#### 1. **Variable rule**

$$
\frac{x : T \in \Gamma}{\Gamma \vdash x : T}
$$

$$
\frac{x : T \in \Gamma, y : U}{\Gamma, y : U \vdash x : T}
$$

$$
\frac{x : T \in \Gamma, y : U, z : V}{\Gamma, y : U, z : V \vdash x : T}
$$

**Proof**:

If a variable $x$ is declared with type $T$ in the context $\Gamma$, then $x$ has type $T$ under $\Gamma$. The type of $x$ remains unchanged.

#### 2. **Literal rule**

Literals (integers, booleans, strings) have fixed types: `int`, `bool`, and `string`, respectively.The type of a literal does not depend on the context $\Gamma$, so the rule holds even if additional variables are added to $\Gamma$.

#### 3. **Function application rule**

$$
\frac{\Gamma \vdash e_1 : T_1 \rightarrow T_2 \quad \Gamma \vdash e_2 : T_1}{\Gamma \vdash (e_1 \, e_2) : T_2}
$$

$$
\frac{\Gamma, x : T \vdash e_1 : T_1 \rightarrow T_2 \quad \Gamma, x : T \vdash e_2 : T_1}{\Gamma, x : T \vdash (e_1 \, e_2) : T_2}
$$

$$
\frac{\Gamma, y : U, z : V \vdash e_1 : T_1 \rightarrow T_2 \quad \Gamma, y : U, z : V \vdash e_2 : T_1}{\Gamma, y : U, z : V \vdash (e_1 \, e_2) : T_2}
$$

**Proof**:

If $e_1$ is a function of type $T_1 \rightarrow T_2$ and $e_2$ is an argument of type $T_1$, then applying $e_1$ to $e_2$ yields a result of type $T_2$. The function application depends only on the types of $e_1$ and $e_2$.

#### 4. **Function abstraction rule**

$$
\frac{\Gamma, x : T_1 \vdash e : T_2}{\Gamma \vdash (\lambda x . e) : T_1 \rightarrow T_2}
$$

$$
\frac{\Gamma, x : T_1, y : U \vdash e : T_2}{\Gamma, y : U \vdash (\lambda x . e) : T_1 \rightarrow T_2}
$$

$$
\frac{\Gamma, x : T_1, y : U, z : V \vdash e : T_2}{\Gamma, y : U, z : V \vdash (\lambda x . e) : T_1 \rightarrow T_2}
$$

**Proof**:

If $e$ has type $T_2$ under a context extended with $x : T_1$, then the lambda abstraction $\lambda x . e$ has type $T_1 \rightarrow T_2$. Lambda abstraction only depends on $x$ and $e$.

#### 5. **Let binding rule**

$$
\frac{\Gamma \vdash e_1 : T_1 \quad \Gamma, x : T_1 \vdash e_2 : T_2}{\Gamma \vdash (\text{let} \, x = e_1 \, \text{in} \, e_2) : T_2}
$$

$$
\frac{\Gamma, y : U \vdash e_1 : T_1 \quad \Gamma, y : U, x : T_1 \vdash e_2 : T_2}{\Gamma, y : U \vdash (\text{let} \, x = e_1 \, \text{in} \, e_2) : T_2}
$$

$$
\frac{\Gamma, y : U, z : V \vdash e_1 : T_1 \quad \Gamma, y : U, z : V, x : T_1 \vdash e_2 : T_2}{\Gamma, y : U, z : V \vdash (\text{let} \, x = e_1 \, \text{in} \, e_2) : T_2}
$$

**Proof**:

If $e_1$ has type $T_1$ under $\Gamma$ and $e_2$ has type $T_2$ under $\Gamma$ extended with $x : T_1$, then the `let` expression has type $T_2$. Let binding only depends on $x$, $e_1$, and $e_2$.

#### 6. **If expression rule**

$$
\frac{\Gamma \vdash e_1 : \text{bool} \quad \Gamma \vdash e_2 : T \quad \Gamma \vdash e_3 : T}{\Gamma \vdash (\text{if} \, e_1 \, e_2 \, e_3) : T}
$$

$$
\frac{\Gamma, x : T \vdash e_1 : \text{bool} \quad \Gamma, x : T \vdash e_2 : U \quad \Gamma, x : T \vdash e_3 : U}{\Gamma, x : T \vdash (\text{if} \, e_1 \, e_2 \, e_3) : U}
$$

$$
\frac{\Gamma, y : U, z : V \vdash e_1 : \text{bool} \quad \Gamma, y : U, z : V \vdash e_2 : T \quad \Gamma, y : U, z : V \vdash e_3 : T}{\Gamma, y : U, z : V \vdash (\text{if} \, e_1 \, e_2 \, e_3) : T}
$$

**Proof**:

If $e_1$ has type `bool`, and both $e_2$ and $e_3$ have the same type $T$, then the `if` expression has type $T$.

#### 7. **Introduction rule**

$$
\frac{\Gamma, x : T_1 \vdash e : T_2}{\Gamma \vdash (\lambda x . e) : T_1 \rightarrow T_2}
$$

$$
\frac{\Gamma, x : T_1, y : U \vdash e : T_2}{\Gamma, y : U \vdash (\lambda x . e) : T_1 \rightarrow T_2}
$$

$$
\frac{\Gamma, x : T_1, y : U, z : V \vdash e : T_2}{\Gamma, y : U, z : V \vdash (\lambda x . e) : T_1 \rightarrow T_2}
$$

**Proof**:

If $e$ has type $T_2$ under a context extended with $x : T_1$, then the lambda abstraction $\lambda x . e$ has type $T_1 \rightarrow T_2$.

#### 8. **Instantiation rule**

$$
\frac{\Gamma \vdash e : \forall \alpha . T}{\Gamma \vdash e : T[\alpha \mapsto S]}
$$

$$
\frac{\Gamma, x : U \vdash e : \forall \alpha . T}{\Gamma, x : U \vdash e : T[\alpha \mapsto S]}
$$

$$
\frac{\Gamma, y : U, z : V \vdash e : \forall \alpha . T}{\Gamma, y : U, z : V \vdash e : T[\alpha \mapsto S]}
$$

**Proof**:

If $e$ has a polymorphic type $\forall \alpha . T$, then $e$ can be instantiated with a specific type $S$ by substituting $\alpha$ with $S$.

#### 9. **Implication rule**

$$
\frac{\Gamma, x : T_1 \vdash e : T_2}{\Gamma \vdash (\lambda x . e) : T_1 \rightarrow T_2}
$$

$$
\frac{\Gamma, x : T_1, y : U \vdash e : T_2}{\Gamma, y : U \vdash (\lambda x . e) : T_1 \rightarrow T_2}
$$

$$
\frac{\Gamma, x : T_1, y : U, z : V \vdash e : T_2}{\Gamma, y : U, z : V \vdash (\lambda x . e) : T_1 \rightarrow T_2}
$$

**Proof**:

If $e$ has type $T_2$ under a context extended with $x : T_1$, then the lambda abstraction $\lambda x . e$ has type $T_1 \rightarrow T_2$.

#### 10. **Implication elimination rule**

$$
\frac{\Gamma \vdash e_1 : T_1 \rightarrow T_2 \quad \Gamma \vdash e_2 : T_1}{\Gamma \vdash (e_1 \, e_2) : T_2}
$$

$$
\frac{\Gamma, x : T \vdash e_1 : T_1 \rightarrow T_2 \quad \Gamma, x : T \vdash e_2 : T_1}{\Gamma, x : T \vdash (e_1 \, e_2) : T_2}
$$

$$
\frac{\Gamma, y : U, z : V \vdash e_1 : T_1 \rightarrow T_2 \quad \Gamma, y : U, z : V \vdash e_2 : T_1}{\Gamma, y : U, z : V \vdash (e_1 \, e_2) : T_2}
$$

**Proof**:

If $e_1$ is a function of type $T_1 \rightarrow T_2$ and $e_2$ is an argument of type $T_1$, then applying $e_1$ to $e_2$ yields a result of type $T_2$.

#### 11. **Generalization rule**

$$
\frac{\Gamma \vdash e : T \quad \alpha \notin \text{free}(\Gamma)}{\Gamma \vdash e : \forall \alpha . T}
$$

$$
\frac{\Gamma, x : U \vdash e : T \quad \alpha \notin \text{free}(\Gamma, x : U)}{\Gamma, x : U \vdash e : \forall \alpha . T}
$$

$$
\frac{\Gamma, y : U, z : V \vdash e : T \quad \alpha \notin \text{free}(\Gamma, y : U, z : V)}{\Gamma, y : U, z : V \vdash e : \forall \alpha . T}
$$

**Proof**:

If $e$ has type $T$ and $\alpha$ is not free in $\Gamma$, then $e$ can be generalized to have the polymorphic type $\forall \alpha . T$. Generalization depends only on the type of $e$ and the absence of $\alpha$ in $\Gamma$.

#### 12. **Substitution rule**

$$
\frac{\Gamma \vdash e : T \quad \Gamma \vdash S : U}{\Gamma \vdash e[S/x] : T[S/x]}
$$

$$
\frac{\Gamma, x : U \vdash e : T \quad \Gamma, x : U \vdash S : V}{\Gamma, x : U \vdash e[S/x] : T[S/x]}
$$

$$
\frac{\Gamma, y : U, z : V \vdash e : T \quad \Gamma, y : U, z : V \vdash S : W}{\Gamma, y : U, z : V \vdash e[S/x] : T[S/x]}
$$

**Proof**:

If $e$ has type $T$ and $S$ has type $U$, then substituting $S$ for $x$ in $e$ yields a term of type $T[S/x]$.

#### 13. **Unification rule**

$$
\frac{\Gamma \vdash e_1 : T_1 \quad \Gamma \vdash e_2 : T_2 \quad T_1 \sim T_2}{\Gamma \vdash e_1 : T_1}
$$

$$
\frac{\Gamma, x : U \vdash e_1 : T_1 \quad \Gamma, x : U \vdash e_2 : T_2 \quad T_1 \sim T_2}{\Gamma, x : U \vdash e_1 : T_1}
$$

$$
\frac{\Gamma, y : U, z : V \vdash e_1 : T_1 \quad \Gamma, y : U, z : V \vdash e_2 : T_2 \quad T_1 \sim T_2}{\Gamma, y : U, z : V \vdash e_1 : T_1}
$$

**Proof**:

If $e_1$ has type $T_1$, $e_2$ has type $T_2$, and $T_1$ unifies with $T_2$, then $e_1$ retains its type $T_1$.

### License

Copyright © 2025 Elric Neumann. MIT License.
