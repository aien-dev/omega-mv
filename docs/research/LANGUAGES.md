# What every language learned, and what Omega takes from it

Status: study, 2026-10-22 (UTC), ledger L409. Extends the B-to-C study (~/handoffs/2026-10-19-b-to-c-archives.md, ledger
L141), which produced decisions 16 to 24 in docs/PROGRAM.md (branch doc/program). Decision 25 on that branch is
already taken (competitors are baselines; the target is the physical limit), so the decisions proposed here start at
26. Every new decision is PROPOSED: docs/PROGRAM.md says Drake decides when a proposed decision becomes a decision.
Companion design: docs/research/COMPATIBILITY.md (how every language calls Omega and Omega calls them).

Outside claims carry [n]; the sources at the end list each page with the sentence copied from it. Each quoted
sentence was re-fetched and re-found by a script before this document was written. Designers' own words come first;
where a designer source could not be fetched (several HOPL papers sit behind a paywall that refused the fetch), the
section says so and uses the nearest source written by the designers or their team.

## Words used here, defined once

- **Compiler**: a program that turns source text into machine instructions ahead of time. **Interpreter**: a
  program that reads source and carries it out directly, step by step.
- **Type**: a label saying what kind of value something is (a whole number of a given width, a float, an array), so
  the compiler knows its size and which operations apply. **Type checking**: refusing programs that misuse types.
  **Type inference**: the compiler working out types the programmer did not write.
- **Garbage collector (GC)**: part of a running program that finds memory nobody uses any more and frees it.
- **Null**: a special "points at nothing" value that a reference may hold.
- **Exception**: a way for an error to jump out of the current function to some handler further up.
- **ABI** (application binary interface): the machine-level rules for how compiled code calls other compiled code.
  **C ABI**: the version of those rules C compilers follow on a platform; every major language can call it.
- **FFI** (foreign function interface): a language's way to call code written in another language.
- **Bytecode / intermediate representation (IR)**: a compact instruction format for an imaginary machine, turned into
  real machine code later. Omega's checked binary IR is one.
- **Kernel** (GPU): a small program run by thousands of GPU threads at once. **Thread**: one independent stream of
  instructions. **Tile**: a small fixed-shape block of an array, processed as one unit.
- **Package manager**: a tool that fetches and builds the libraries a program depends on.
- **HOPL**: the ACM History of Programming Languages conferences, where designers write their own histories.
- **Generics**: writing one piece of code that works for many types (one sort routine for any kind of list), with
  the compiler filling in the type.
- **Reference counting**: freeing memory by keeping a count of how many places point at each object; when the count
  reaches zero the object is freed.
- **Dangling pointer**: an address that still points at memory after that memory was freed or reused.
- **Purity / laziness / monads** (Haskell): a pure function only computes its result and changes nothing else;
  lazy evaluation computes a value only when it is first needed; a monad is Haskell's way of writing steps that do
  change things (input, output, state) while keeping the rest pure.
- **Linker**: the tool that joins separately compiled pieces into one program or library. **Image** (Smalltalk,
  Lisp): a saved snapshot of the whole running system, code and data together.
- **Crate** (Rust): one package of Rust code. **Superset**: a language that accepts every program of another
  language, plus more.
- **DSL** (domain-specific language): a small language for one kind of job. **MLIR**: a shared compiler toolkit
  from the LLVM project that several AI compilers build on.
- **Shared memory** (GPU): a small fast memory that the threads of one block share. **Coalescing**: arranging
  neighbouring threads to read neighbouring addresses so the memory system can serve them in one go.
  **Vectorisation**: using one instruction to work on several numbers at once. **Tensor cores**: GPU units built to
  multiply small matrices in one step.
- **Stride**: how far to step in memory to reach the next element along one dimension of an array.
- **Digest**: a short fingerprint computed from a file's bytes; change any byte and the fingerprint changes.
- **Omegese**: Omega's source language. **omc**: the Omega compiler being built under ledger L408. **checker2**:
  Omega's independent checker, which reads the compiled IR and refuses unsafe programs.
- **Gate**: an automatic test that must pass before work counts as done. **Rung**: one step on a list of steps,
  each with its own gate. **Bar**: a milestone in docs/OMEGA.md. **Motor**: the AIEN model-serving engine.
- **Receipt** (Omega): a signed record that a program passed the checker (checker2).

## How to read this

Part 1 takes the languages roughly in the order they appeared, grouped by what they were for. Each one answers four
questions: what it was built for, what it got right that lasted, what it got wrong (in the designer's own words where
they said so), and how it spread and talks to other languages. Each ends with one line on what Omega takes from it.
Python gets a longer section because it is the model Drake named for "rules them all". Part 2 is one table: each
lesson, the languages that show it, the evidence, and Omega's yes or no, numbered from 26. Then the conflicts with
decisions 16 to 24, and the lessons Omega already follows with the gate that proves each.

## Part 1. The languages

### The first languages: speed, symbols and business

#### Fortran

**Built for.** Scientists on IBM machines in 1954, when programmers cost as much as the computer itself [1]. The
team's bet was machine code nearly as fast as hand-written code; Backus put that ahead of language design and still
defended the choice in 1978 [2].

**Right, and it lasted.** Backus quotes a report that more than half of the machine instructions run on those IBM machines came out of the
Fortran compiler [3]. Its rule that a subroutine may not reach the same storage through two argument names when
one is written lets compilers assume no overlap and reorder code freely [4]; the rule dates from Fortran II [5].
That is decision 18, held since the 1950s.

**Wrong.** The team expected Fortran to nearly end debugging, so the first release had weak syntax checking; better
checks came with Fortran II [6]. Backus's own later verdict on the family of conventional languages that Fortran
started: large and weak, tied to word-at-a-time programming [7]. The overlap rule is a promise the programmer
makes; the compiler does not check it.

**How it spread, how it talks.** Shared code: Dijkstra, a critic, said physicists stayed with Fortran to share
programs, bugs included [8]. Since Fortran 2003 there is a standard way to declare procedures and data that C can use [9], with stated gaps
(no unsigned integers, no variable-argument functions) [10].

**For Omega.** Keep the no-overlap rule, but as a proof the checker makes (decision 18), never as an unchecked promise.

#### Lisp

**Built for.** Programs that work on symbols and lists rather than numbers, by John McCarthy [unverified: this one-line framing; everything below is from his own history].

**Right, and it lasted.** Garbage collection: McCarthy rejected reference counts because the IBM 704's word had too
few spare bits, and chose a collector instead [11]. Programs as data: because a Lisp program is a list, the
interpreter reads it as ordinary data, which made programs that write programs easy [12]. The interpreter itself
came from a student hand-coding McCarthy's paper definition of `eval` [13].

**Wrong.** Early numeric work ran far slower than Fortran, because fast arithmetic needs type information early Lisp
lacked [14]. The planned friendly notation was never finished [15]. In 1979 McCarthy wrote that nobody had tried to
promote Lisp or agree a standard, a stated cause of its many dialects [16].

**For Omega.** Omega's programs already are data (the binary IR the AI writer emits and checker2 reads). No GC
(decision 24). One specification and one checker from the start, so there are no dialects.

#### COBOL

**Built for.** Business data processing on any maker's computer, written in English-like words so managers could
read it [17].

**Right, and it lasted.** The English-like text documented itself [18]; programs moved between machines that had a
COBOL processor [19]. It spread because the US Defense Department would not buy computers that lacked a COBOL
compiler, so every maker built one [20]. (Jean Sammet's HOPL paper could not be fetched; her oral history is used.)

**Wrong.** Two-digit years were common in the application and database files of large data-processing shops [21], one root of the Y2K repairs that cost US federal agencies
alone about 8.4 billion dollars, across all languages [22]. Sammet regrets that a macro-like feature was dropped
because nobody had implemented it, and vendors later sold the same idea back [23]. Dijkstra's attack on COBOL [24]
is an opinion, not a measurement.

**For Omega.** Widths are explicit (decision 20), so a field's size is a decision written in the program, never a
habit. A mandate gets compilers built; it does not by itself make a language good.

#### Algol 60

**Built for.** One common, machine-independent language for algorithms, defined by an international committee [unverified: this framing]. (Naur's and Perlis's
HOPL papers could not be fetched; the Revised Report itself, written by the designers, is used.)

**Right, and it lasted.** The syntax was defined in a formal notation, later called BNF [25]; Wirth calls it the
first language with a precise formal syntax [26]. Blocks declare names that exist only inside them [27]. Hoare says
recursion, then controversial, made his compiler possible [28]. Hoare's compiler checked every array subscript, and
years later his customers urged him not to add an option to switch the checks off [29]; he says designers still had
not learned that lesson in 1980 [30].

**Wrong.** Call by name re-evaluates the caller's expression every time a parameter is used [31]; Lindsey, editor of
the Algol 68 report, writes that it is nowadays regarded as obsolete [32]. The report left input, output and the character set to
each machine, so portability rested on conventions outside it [33].

**For Omega.** Omegese already has a fixed grammar the tool can enumerate (`omegese next`). Keep bounds checks with no
off switch: removed only where the checker proves them (decision 23), never by a flag (decision 30).

#### Algol 68

**Right.** Orthogonality: a small set of ideas that combine freely, Lindsey's main claim for it [34].

**Wrong.** Members of the committee signed a minority report calling the language and
its description method a failure [35] and obsolete as a tool for reliable programs [36]. Most implementations that
were started were never finished [37]; Wirth says it faded quickly [38]. Lindsey admits two of his own mistakes:
the flexible-array facility [39], and removing bounds from routine parameters, which he now thinks would have worked
as checkable preconditions [40].

**For Omega.** A specification has to be small enough that complete implementations exist. Shapes on parameters are
worth keeping, as preconditions the checker proves (decision 23).

#### BASIC

**Built for.** Every Dartmouth student, at a time-sharing terminal; no existing language was easy enough [41].
Numbered lines let a student fix a line by retyping it on a slow teletype [42].

**Wrong.** Jumps went to line numbers only [43]. On small home computers, makers' compromises violated the
designers' principles and became permanent [44], until no two implementations were compatible [45]. Dijkstra's
remark about BASIC damaging beginners [46] is an opinion. (Kurtz's HOPL paper could not be fetched; the designers'
book "Back to BASIC" is used.)

**For Omega.** Structured control only (Omegese rule 3). One specification, one checker: an implementation that
cannot do a feature refuses it by name instead of inventing its own.

#### Pascal

**Built for.** Teaching, by Niklaus Wirth: every constant, variable and function had a fixed, static type [47]. (Wirth's HOPL "Recollections" could not be
fetched; his "50 Years of Pascal" is used.)

**Right, and it lasted.** Pascal-P, a compiler for an imaginary simple computer, meant a new site only had to
implement that machine [48]; groups at San Diego and Borland packaged it for small computers and schools [49].

**Wrong.** Array size was part of the array's type, so one routine could not sort arrays of different lengths
(Kernighan) [50]; Kernighan found standard Pascal inadequate for real programs [51] and closed to using other
people's software [52]. Wirth's first compiler plan, hand-translating Pascal into Fortran, failed: structured code did
not translate into an unstructured language [53].

**For Omega.** Length travels with the buffer as data (decision 16), so one routine serves every length; the shape is
checked, not baked into the type's identity. Pascal-P's imaginary machine is the idea behind Omega's checked IR.

### Objects, abstraction and types

#### Simula

**Built for.** Operations-research workers describing simulations of interacting parts [54]. Classes and subclasses
came out of solving two concrete problems in describing simulated processes, not a plan for a new paradigm [55].

**Right, and it lasted.** The class idea itself; Stroustrup took classes from Simula as the primary focus of program design [56]. Simula 67 survived because
one large manufacturer committed to build compilers [57].

**Wrong.** The authors list choices they regret, such as modelling virtual procedures on Algol parameters, which
forced run-time checks of arguments [58]. Memory management was treated as risky: reference counting plus a last
resort collector, later a heavy cost [59]. Stroustrup found Simula's run-time cost could not be fixed, and that
shaped C++ [60].

**For Omega.** Good ideas come from narrow real problems; Omega's narrow problem is one model on one machine.

#### Smalltalk

**Built for.** Alan Kay's picture of every object as a small computer receiving messages [61]. Kay says the big idea
was messaging, not classes, and he regrets the word "objects" [62]; messaging was never finished at Xerox PARC [63].
(The HOPL paper is read from a copy, because the ACM page refused the fetch.)

**Right, and it lasted.** After the 1981 articles and 1983 books, others could rebuild the virtual machine and ship
their own image of basic classes [64].

**Wrong, for talking to others.** The image is a self-contained world: Squeak wanted every part changed in Smalltalk
alone [65, 66], and its FFI to outside libraries is an add-on outside that world [67]. Its own community calls
plugins the safe route because users can call nothing beyond what the plugin provides [68].

**For Omega.** A closed, self-describing world is easy to check and hard to connect. Design the door first
(COMPATIBILITY.md), and make it as narrow as a plugin: only what the interface description lists.

#### C++

**Built for.** Hardware-level work with Simula-style organisation [69]: use ordinary linkers, stay compatible with
C, leave no lower-level language below it except assembler [70].

**Right, and it lasted.** Zero overhead enforced by measurement: a 3 percent slowdown against C was treated as
unacceptable and removed [71]. Building on C put it where systems programmers already were [72, 73].

**Wrong.** Stroustrup calls the complexity cost of the C base considerable [74]. C compatibility forced a feature
(automatic type deduction) out in 1983 that came back decades later [75]. Three demands conflict: simplify, add
features, never break old code [76]; because of the code already written, nothing is ever removed [77]. Exceptions
and run-time type information cost enough that embedded and high-performance builds switch them off [78]. Namespaces
came too late [79]. Name mangling squeezes overloaded names into plain C linker names [80]; a stable C++ binary
interface is a hard problem and part of why people ask for C interfaces [81, 82].

**For Omega.** Measure overhead as C++ did, against the machine (decision 25). Do not promise a rich binary
interface; meet the world at the C ABI. Keep the right to remove things (decision 27).

#### Ada

**Built for.** The US Defense Department's problem of hundreds of incompatible languages, each tied to one machine
and weapon system [83]. The Steelman requirements asked the language to avoid error-prone features and detect errors
automatically [84], to put range, precision and index limits on types [85], and to rank readability above ease of
writing [86]. (No paper by Ichbiah, the lead designer, could be fetched; Whitaker chaired the working group.)

**Right, and it lasted.** Every compiler was validated and subsets were refused, to keep code portable [87]. SPARK,
an Ada-derived language, removes undefined behaviour so programs can be proved free of run-time errors [88], at the
price of extra work by the developer [89]. Ada's standard includes a way to call C [90].

**Wrong.** The planners accepted large, costly compilers [91], and the cost fell on a small defence market while
cheap compilers for small computers spread elsewhere (independent analysis) [92]. Whitaker's own accounting says the
cost goal was missed [93] and the shared tool community he expected did not appear [94]. The Defense Department
later dropped its mandate [95]. Ariane 5 flight 501 failed on an unprotected overflow carried from an old design to
new hardware: checks do not help when the assumption itself moves [96].

**For Omega.** Validation of every implementation and no subsets: yes, through checker2 and receipts. No undefined
behaviour makes proof possible (decision 19, SPARK's lesson). A specification is only as good as its assumptions:
gates must run on the real machine, not only in simulation.

#### ML and OCaml

**Built for.** Robin Milner's work on machine-checked proofs, then general programming [unverified: this framing].

**Right, and it lasted.** Milner's type checker trapped a large share of errors in the 1970s [97] while inferring
types the programmer did not write [98]. Standard ML spread datatypes with pattern matching, inference and generics
[99]; Rust's reference names ML and OCaml as their source [100]. OCaml gained users even when slow, then a fast
portable bytecode version widened use [101, 102].

**Wrong.** The Standard ML designers admit the definition said nothing about calling foreign code, so such programs
do not move between compilers [103], and they list specific design mistakes [104]. OCaml's C stubs must cooperate
with the garbage collector through special macros [105], parts of the runtime used from C carry no stability promise
[106], and the global collector lock was removed only in OCaml 5, in 2022 [107].

**For Omega.** Inferred types inside, written types at the edges (Omegese rule 2). Put the foreign door in the
specification itself, not in one compiler (decision 31).

#### Haskell

**Built for.** One common lazy functional language for a research community that had several [unverified: this framing]. Once laziness was chosen, purity followed; the
authors call purity a big bet [108].

**Right, and it lasted.** Purity forced the work on monads, its main payoff [109], which solved input and output,
previously embarrassing in a pure language [110]. Type classes, proposed by Wadler in 1988 [111], became Rust's traits
[112]; Swift's designer lists Haskell among Swift's sources [113].

**Wrong.** Laziness made memory use hard to predict even for experts [114]; space leaks forced strictness features
into a lazy language [115]. Its package system, Cabal, was started only in 2004 [116], and the authors write that it took more than fifteen years for such tools to become important
[117]. The authors say Haskell stayed free to change because it never became too successful [118].

**How it talks.** Haskell's standard foreign interface treats C
as the lowest common denominator [119], specifying only the C calling convention [120].

**For Omega.** Eager evaluation and memory fixed at check time (decision 24): memory use is known before the program
runs. Ship packaging with the language (decision 21's digests).
### Reliability, glue, scripting, the web and managed platforms

#### Erlang

**Built for.** Ericsson's telephone switches, where a process or hardware failure must only affect the call it was
handling [121]. Joe Armstrong's thesis starts from the premise that large programs will contain errors in service, and
asks how to stay reliable anyway [122]. (The ACM page for his HOPL paper refused the fetch; a course copy of the full
paper is used.)

**Right, and it lasted.** Processes share no memory and talk only by asynchronous messages [123, 124]; copying
messages was chosen to avoid dangling pointers and gave isolation, concurrency and easier distribution [125]. "Let it
crash": a process that meets an error it cannot handle stops, and a supervisor process deals with it [126]. Two
versions of a module can be loaded at once, which allows upgrades without stopping [127].

**Wrong or costly, in Armstrong's words.** Copying everything costs speed, and he says so: everyone told the team data
must be shared for speed, and he holds that a slow system that recovers beats a fast one that cannot [128]. The first
prototype was far too slow for a product [129].

**How it talks.** Device drivers were planned in C from the start [130]. The safe way out is a port, a byte channel to
a program in a separate operating-system process [131]. A faulty port driver affects the whole runtime [132]; a
native function (NIF) that crashes takes down the whole virtual machine [133], because the VM gives native code no
scheduling and no memory protection [134].

**For Omega.** Isolation over shared memory (decision 37). A failure ends one call, not the host: a trap returns its
code through the door and the caller keeps running (decision 29, COMPATIBILITY.md section 3.4). Erlang's NIF warning is
the reason Omega's door checks everything it can before any Omega code runs.

#### Perl

**Built for.** Text manipulation, practical rather than beautiful, as its own introduction says [135, 136].

**Right, and it lasted.** CPAN, its module archive, is how Perl reached every field [137]. Larry Wall's slogan,
"There's More Than One Way To Do It", framed as freedom of choice [138]. Its ideas passed on: Wall says Ruby borrowed
much from Perl and competes for the same space [139].

**Wrong.** The many-ways design costs readability, and Perl's own style guide tells programmers to choose the most
readable way because the language will not [140]; its shortcuts are for throwaway scripts [141]. In October 2000 Wall
announced Perl 6 as the first chance to break compatibility [142, 143]. Its first official release came at Christmas
2015 [144], about fifteen years later. The shared name and long delay hurt both languages [145, 146], and Perl 6 was
renamed Raku in October 2019 [147]; Perl's site now treats Raku as a separate sister language [148].

**How it talks.** Through XS, an interface description language, or the SWIG wrapper generator [149], with libffi
also available [150].

**For Omega.** One way to write each thing: Omegese's grammar gives one legal next word (decision 22, already). A
rewrite announced as the next version of the same language, with no way to run old and new side by side, split
Perl's community for fifteen years (decision 27).

#### Python

Python is the model Drake named for a language that "rules them all". This section explains how it got there and what
it paid, in its designers' words.

**Built for.** Guido van Rossum was in the Amoeba operating-system group at CWI and needed something better than C
programs or shell scripts for system administration [151]. From the start he chose a language that was "generally
extensible" rather than tied to one system [152], and he listed several levels of extensibility among his design
rules [153]. Speed was put off on purpose [154].

**Why it rules them all.** Five choices reinforce each other:

1. **The C API and extension modules.** The official guide gives two reasons to write a C extension: to reach native
   libraries, and to avoid interpreter overhead [155]. Any C library can be wrapped and then imported like a Python
   module. A stable subset of the API lets one compiled module keep working across Python 3 releases [156, 157].
2. **Glue with no compiler.** ctypes provides C-compatible data types and calls functions in shared libraries [158]; cffi does the same,
   and recommends its compiled mode as the faster and safer one [159, 160].
3. **Batteries included.** The standard library is a stated philosophy: large, working packages ship with the
   language [161].
4. **Readability.** The Zen of Python, recorded in PEP 20, puts readability beside simplicity [162].
5. **NumPy and the buffer protocol.** PEP 3118, written by NumPy's creator Travis Oliphant and Carl Banks, lets any
   extension share raw memory with any other without copying [163], using NumPy's model of a pointer plus shape and
   strides, so a slice of a big array needs no copy [164]. The NumPy array is memory plus type, shape and strides, in
   C or Fortran order [165], and almost every numerical Python library is built on it [166]. That is the road by which
   science and then machine learning moved to Python.

Put together: Python does not rule other languages by replacing them. It rules them by calling them cheaply, through
a small C-level interface and a shared description of raw memory. Its speed came from the C, C++ and Fortran
libraries it calls; wheels, Python's binary package format, exist because most users cannot build those compiled
libraries themselves [167].

**What it paid, in the team's words.**

- **The global interpreter lock (GIL).** One lock lets only one thread run Python code at a time; the glossary says it
  exists to keep CPython's object model simple [168]. PEP 703 calls it an obstacle to using multi-core processors
  [169]; the free-threaded build that removes it slows single-thread code [170] and is not compatible with the stable
  binary interface, so every extension must be rebuilt [171].
- **Reference counting at the border.** C code sharing Python objects must follow ownership rules for every
  reference [172].
- **Speed.** The rule to put speed work off left a great deal: the Faster CPython work reported Python 3.11 as 10 to 60
  percent faster than 3.10 [173].
- **Packaging.** PEP 517 lists three problems with the old build path and aims to stop one mailing list from being the
  gatekeeper for every build system [174].
- **The 2-to-3 split.** Guido's notes call Python 3.0 the first intentionally backwards-incompatible release [175].
  The reason was text versus bytes: Python 2 kept 8-bit strings next to Unicode to
  stay compatible with Python 1 [176]. Work began in 2006 [177]; the end of Python 2 was announced for 2015 and pushed back to
  2020 [178]; support ended on 1 January 2020 [179]. Roughly fourteen years of two incompatible Pythons.

**For Omega.** Copy the part that made Python the glue: a small, fixed C-ABI interface and a shared description of
array memory (decisions 26 and 35; COMPATIBILITY.md). Do not copy the costs: no global lock (decision 37), no
reference counts at the border because ownership never crosses (COMPATIBILITY.md section 4), and incompatible changes
as versions that run side by side, never a split (decision 27). And make Omega callable from Python first, so Python
stays the glue and Omega becomes one more fast library it calls.

#### Ruby

**Built for.** How programming feels. Matz (Yukihiro Matsumoto) argues all languages can do the same work, so he
designed for the programmer's experience [180]; he says Ruby suits him and may not suit everyone [181]. He wanted
something more powerful than Perl and more object-oriented than Python, blending Perl, Smalltalk, Eiffel, Ada and Lisp
[182]. Consistency is a tool for him, not a goal [183].

**Right, and it lasted.** Rails drove its growth [184], and the Rails creator traces Rails' first principle to Ruby's
programmer happiness [185]. RubyGems.org hosts the community's packages [186]; Bundler pins exact versions across
development and production [187].

**Wrong or costly.** CRuby threads share one global lock; Ractors, added in Ruby 3.0, each hold their own so they can
run in parallel [188], and are still labelled experimental [189]. The move from 1.8 to 1.9 was harder than the team
wanted, as the 2.0 notes admit [190]; 1.8.7 stayed under maintenance years after 1.9 arrived [191].

**How it talks.** C extensions must convert between C's typed variables and Ruby's typed values by hand [192], and a
wrong conversion is a serious problem [193]. The FFI gem binds shared libraries with no hand-written C [194].

**For Omega.** Exact dependency pinning (decision 36, by digest). No global lock (decision 37). Ruby reaches Omega
through the FFI gem with no glue (COMPATIBILITY.md section 5).

#### Lua

**Built for.** A small configuration language at PUC-Rio in Brazil in 1993 [195]. The designers' goals were
simplicity, small size, portability, speed and easy embedding [196]. Lua is a library with no `main` of its own; the
command-line program is only one host [197].

**Right, and it lasted.** One data structure, the table, for everything; the team resisted pressure to add arrays and
tuples [198]. Lua 4.0 replaced direct object handles with a virtual stack between C and Lua and removed all global
state so several Lua instances can live in one program [199]; C never holds a direct reference to a Lua value, so the
collector is never surprised [200].

**Wrong, in the designers' words.** No boolean type at first, which left two false values [201]; string-to-number
conversion copied from Awk, which they could have left out [202]; no policy for modules and packages, which kept groups
from sharing code [203]. Changing the C interface in 4.0 forced every C function to be rewritten, and Tecgraf, where
Lua began, never moved to 4.0 [204]. Counting arrays from 1 is defended, not regretted: it came from early Fortran
users [205].

**How it talks.** The designers argue a plain FFI covers common needs but embedding needs closer integration [206].

**For Omega.** No global state and no runtime: Omega's output can sit inside any host (decision 45). Never change the
door's rules in place (decision 27).

#### Java

**Built for.** Networks of mixed machines: write once, run on many systems [207], through an interpreted bytecode
that solves both shipping binaries for many platforms and versioning [208]. No pointers and a garbage collector, to
remove the memory bugs of manual management [209, 210].

**Right, and it lasted.** The bytecode and virtual machine model [208] and memory safety by default [209]; C# and Kotlin later built on the same kind of managed platform [211, 212].

**Wrong or contested.** Checked exceptions: Gosling defends them as forcing a programmer to take a position on errors
[213]; Hejlsberg (the C# designer, not on the Java team) argues they break callers whenever an API adds one [214] and
get worked around with empty handlers at scale [215]. Generics were erased at compile time to stay compatible with
existing compiled code [216]; Goetz calls it the pragmatic choice for 2004 [217]; the cost is that a program cannot ask
a list what it holds [218]. Null came along from earlier languages; Tony Hoare calls his 1965 invention of the null
reference his billion-dollar mistake [219].

**How it talks.** The Java team calls JNI's machinery brittle [220]: three pieces built with different tools and kept
in sync by hand [221], no mapping of Java objects to C structs [222], gaps third-party libraries grew to fill [223].
Its replacement, the Foreign Function and Memory API, gives every foreign memory region checked address bounds and a
checked lifetime [224].

**For Omega.** No null (decision 28). Errors as values, not exceptions of any kind (decision 29). Foreign memory always
carries its bounds (decision 16 at the door).

#### JavaScript

**Built for.** A small "glue" language beside Java in the Netscape browser, written in ten days [225], for part-time
and non-professional programmers [226].

**Right, and it lasted.** Distribution: it shipped with the browser as a de facto web standard [225]. Node.js later took it to servers and tools,
where many chose it by preference [227].

**Wrong, in the authors' words.** Automatic type conversions were added to lower the entry barrier [228] and became a
significant source of confusion and bugs, especially in `==` [229]. Eich removed most of them in JavaScript 1.2, but it
was judged too late for the standard because it would break pages [230]. "Don't break the Web" keeps mistakes in the
language forever; standards can only add [231]; even a feature on Crockford's list of mistakes stays because developers
use it [232].

**How it talks.** Native add-ons had to be rebuilt for every major Node version until Node-API gave them a C interface
independent of the JavaScript engine [233, 234]. Node 26.1.0 added a built-in, experimental FFI [235].

**For Omega.** No implicit conversions (decision 20, already). Keep a way to remove mistakes: versioned units side by
side, never "only add" (decision 27).

#### PHP

**Built for.** Rasmus Lerdorf's own web pages. He says he never meant to write a language (as reported from a 2012
talk) [236].

**Right, and it lasted.** Fit with the rest of the web: Lerdorf credits the ecosystem, such as Apache, over the
language [237]; internet providers liked it because one copy served many accounts, which spread it through cheap
hosting [238]; in his words, a pleasant language is useless without a way to host it and reach the back end [239].

**Wrong.** Lerdorf calls PHP an admittedly ugly solution that was never about design purity [240]; the official
history says the early syntax was somewhat inconsistent [241]. PHP 6, a rewrite with Unicode throughout, was begun in
2005 and dropped in 2010 [242]; the next release was named PHP 7 and was up to twice as fast as 5.6 [243].

**How it talks.** The FFI extension, implemented in PHP 7.4 [244] and designed after LuaJIT's FFI and Python's CFFI, calls C from plain PHP [245],
so binding a library no longer requires writing an extension against the engine's internals [246].

**For Omega.** Being easy to deploy wins users: Omega's shared object needs no runtime and no install step
(COMPATIBILITY.md). A rewrite that changes everything at once stalls (decision 27).

#### C#

**Built for.** Microsoft's managed platform, with versioning as a design pillar: every feature was checked against how
it behaves as code ages [247]. Design by a small team meeting in one room, by taste [248].

**Right, and it lasted.** Generics built into the runtime, so a list of integers really holds integers and type
information survives to run time [249]. Value types (structs) give control over memory layout inside a collected
language [250]; span types work on a region of memory without copying [251]. Calling native libraries is built into
the runtime as platform invoke [211].

**Wrong, in the team's words.** Before C# 8 the language could not say where null was allowed [252]. Nullable
reference types were added to find null bugs before run time, citing Hoare [253, 254], but they are a compile-time
check only; null still exists underneath [255]. Unsafe code is allowed and documented as unverifiable [256].

**For Omega.** If generics ever come, specialise them per type at check time (decision 39). Retrofitting safety leaves
the old hole underneath; Omega starts without null (decision 28) and without an unsafe form (decision 41).

### Modern systems languages

#### Go

**Built for.** Google's own software-engineering problems: slow builds and large codebases [257], described by its
team as an engineering tool, not a breakthrough [258].

**Right, and it lasted.** Package and import rules measured against C++ cut build work by a large factor [259]. Modules
pick the highest minimum version anyone asked for, never a silently newer one [260]. The Go 1 promise: programs written
for Go 1 keep compiling [261]. Early users came partly for concurrency [262].

**Wrong, in the team's words.** Generics were left out because they are hard to design well, and the team spent about
a decade before finding a design [263, 264]; Russ Cox wrote the dilemma down in 2009 [265], and the 1.18 release notes
admitted the new code had little production testing [266]. A nil pointer stored inside an interface makes the interface
non-nil, which the FAQ calls confusing [267]. After years of proposals, the team stopped pursuing new syntax for error
handling in 2025 [268].

**How it talks.** cgo, by importing a pseudo-package named "C" [269]. An independent engineer lists its costs: more
complex and slower builds, cross-compiling off by default [270], and the loss of Go's script-free build [271]. The Go
team moved its own compiler from C to Go with a translator checked by identical output (COMPATIBILITY.md section 6).

**For Omega.** Compatibility promises and exact dependency selection: yes (decisions 27, 36). Generics: wait for a
measured need (decision 39).

#### Rust

**Built for.** Memory safety without a garbage collector, through compile-time ownership rules [272].

**Right, and it lasted.** Google reports Android's memory-safety share of vulnerabilities fell from 76 to 24 percent
as new code moved to memory-safe languages, Rust among them [273], and Rust changes needed emergency rollback less than
half as often as C++ changes [274]. Editions let the language make breaking syntax changes while crates of different
editions still work together [275]; the editions proposal names splitting the ecosystem as the risk a 2.0 would run
[276]. Cargo builds code and fetches dependencies in one tool [277].

**Wrong, in its author's words.** Graydon Hoare says the Rust that shipped is far from the one he wanted [278]; he
would have traded speed and power for simplicity [279]. Inlining and specialising code across crates is a big part of
slow compiles and of the missing stable ABI [280]; slow compilation tops the project's own 2024 survey [281]. Error
handling at 1.0 was what remained after earlier attempts were removed [282]. The compiler rejects some valid programs
on purpose; `unsafe` is the escape hatch [283].

**How it talks.** `extern "C"` and `#[repr(C)]` [284]. The Rust calling convention has no stability promise [285];
other languages meet Rust at the C ABI [286], and a richer cross-language ABI is still experimental [287]. Google
chose to invest in interoperability over rewrites [288]; the Linux kernel wraps C through generated bindings [289].

**For Omega.** Ownership without a collector: Omegese already has own, edit and view arrays checked by checker2.
Editions over a 2.0 split (decision 27). Refuse what cannot be proven, without an unsafe escape (decisions 18, 41).

#### Swift

**Built for.** Memory safety and a modern language that still talks to Apple's Objective-C and C world; Lattner
started it in 2010 [290, 291].

**Right, and it lasted.** Optionals: a value that might be missing has a different type, so the compiler forces the
check [292]. C and C++ declarations are imported through the Clang compiler [293].

**Wrong or costly.** Apple's executives worried that Swift was risky and could fragment the community [294]. Without source compatibility,
projects were locked to one Swift version [295]. The stable ABI came with Swift 5 [296], declared on
Apple platforms first [297], and library authors and app developers still needed the same compiler until module
stability [298]. Library evolution mode costs speed, and switching it on later is itself a break [299]; freezing a
type for speed exposes its layout forever [300].

**For Omega.** No null (decision 28). A binary interface is a permanent promise; keep Omega's to the C ABI and a
plain-text interface description (decision 26).

#### Zig

**Built for.** Code that does what it looks like: no hidden control flow [301]. The language has no heap
allocation of its own [302]; anything that allocates takes an allocator argument, so the standard library works even on
the freestanding target, with no operating system [303].

**Right.** Compile-time code in the same language instead of macros [304]; `zig cc`, a C compiler with
cross-compiling built in, which gives C projects a reason to adopt the toolchain [305]. The C ABI is the stated way in
and out [306].

**Not yet.** Zig has not reached 1.0 [307]. No designer admission of a design mistake was found.

**For Omega.** Already studied for compile-time evaluation and incremental builds in
docs/osc/ZIG-LESSONS-COMPTIME-INCREMENTAL.md (2026-10-13). Taken here: nothing hidden (decision 42); allocation
decided in the open (decision 24 goes further: at check time).

#### Julia

**Built for.** Ending the two-language problem, where users prototype in an easy language and experts rewrite in C or
Fortran [308]: the launch post asked for one fast, interactive, open language [309]. Multiple dispatch picks a
function's version from the types of all its arguments [310].

**Right.** Calling C and Fortran with no glue code [311]; arrays of plain numbers share C and Fortran layout [312].

**Wrong.** Compiling on first use gave long first-call delays until Julia 1.9 cached native code [313]; the fix made
cache files larger [314]. Arrays count from 1, like Fortran and Matlab [315].

**For Omega.** Compile ahead of time, with the receipt signed before anything runs (decision 33). One language from
the chip to the host is Omega's version of solving the two-language problem.

#### Kotlin

**Built for.** JetBrains' own productivity inside a large Java codebase, with two hard limits: smooth interop with
existing Java, and compiling no slower than Java [316].

**Right.** Interop in both directions, which makes gradual migration possible [212]. Null in the type system [317].
In 2019 Google announced that many new Android Jetpack features would be offered first in Kotlin [318].

**How it talks.** Kotlin/Native reads C headers with cinterop [319], still labelled Beta [320].

**For Omega.** Compile speed as a stated limit (decision 38). Compatibility without being a superset of the older
language (decision 40).

### The GPU languages

#### CUDA C++

**Built for.** Brook, Ian Buck's Stanford project, set out to end GPU programming through graphics tricks [321]; it was
vendor-neutral, with NVIDIA, ATI and CPU back ends [322], and relied on each vendor's shader compiler [323]. NVIDIA calls
Brook the precursor and C familiarity the goal [324]; CUDA arrived in 2006 so any computing job could use the GPU
without a graphics interface [325].

**Right, and it lasted.** The model: a kernel run by huge numbers of threads in blocks in a grid [326], executed in
groups of 32 that may branch at a cost [327]. PTX, a documented virtual instruction set, stays stable while the real
instruction sets change [328], gives forward compatibility [329], and lets other compilers (Triton, Mojo) target the
chip [330]. The closed libraries (cuDNN, cuBLAS, cuFFT) spared developers from writing GPU code [331] and carried CUDA
into AI through TensorFlow and PyTorch [332]; compatibility across generations let gaming cards serve as the install
base [333].

**Wrong or costly.** Explicit memory placement made software harder, so NVIDIA added unified memory that the driver
moves [334, 335]. NVIDIA's current guide adds a tile mode in which the thread index disappears [336, 337]: its own signal
that per-thread code is hard to write well (our reading). PTX is translated by NVIDIA's closed driver as the last step [338]. Code
for one vendor needs significant changes for another [339], and the SDK license forbids translating CUDA output for
non-NVIDIA platforms [340]; a competitor's engineer counts lock-in among the reasons CUDA leads [341]. Operations the
libraries lack need expert-written kernels [342].

**How it talks.** The host side is ordinary C++ built by the system compiler [343].

**For Omega.** Omega already writes the GB10's own machine code with no PTX and no NVIDIA compiler (docs/OMEGA.md
section 2). Take the tile model (decision 34) and treat libraries as part of the language's reach (decision 35).

#### OpenCL

**Built for.** The open, vendor-neutral answer: host code in C or C++, device code compiled separately [344], at run
time or offline to SPIR-V [345].

**Wrong.** Khronos made everything after version 1.2 optional in OpenCL 3.0 [346]; the working group's chair calls
1.2 the baseline vendors need [347]. That is an admission, in effect, that the 2.x line was not adopted (this reading
is ours). NVIDIA's own staff say its drivers stayed on 1.1 for years for business priorities [348]. Apple deprecated it
[349]. Host setup code is long (from Intel, which sells a competing model) [350].

**For Omega.** Optional features become the features nobody can rely on: one specification, no optional parts
(decision 32).

#### GLSL, HLSL and the move to binary IR

**What happened.** GLSL put a full source compiler inside every vendor's driver. Chrome's team fixed hundreds of bugs
from passing shader text to drivers [351] and had to restrict legal shaders so driver compilers would not crash [352];
Google's WebGL team argued such bugs come from text input itself [353]; each vendor's compiler read the same source
differently [354]. The answer was SPIR-V, a binary form handed from the application to the driver [355], removing
each vendor's own high-level translator [356]; a short, pinned-down binary format is easier to match everywhere [357],
and Vulkan's launch said it cuts driver complexity [358], though it does not remove back-end compile time [359]. The
GLSL specification itself now points Vulkan users to offline compilers [360]. Microsoft did the same for HLSL with
DXIL, an LLVM-based contract between shader producers and driver compilers [361, 362], documented to invite other
languages [363]. WebGPU's shader language became WGSL, a new statically typed text language [364], written after that argument [353]; its specification defines results
where C and C++ leave them undefined [365] and admits some behaviour still differs between machines [366].

**For Omega.** Checked binary IR is what crosses to the machine, never source text (decision 33, already true).

#### Triton

**Built for.** Tiled neural-network kernels: the tile, a statically shaped sub-array, is the unit [367], because the
specialised languages of the time ran slower than vendor libraries [368].

**Right.** No thread-level code: one program per block, with synchronisation handled by the compiler [369]; the
compiler does coalescing, vectorisation, tensor-core instruction choice and shared memory [370, 371]. Kernels are
decorated Python functions [372]; PyTorch tensors arrive as pointers [373]; DLPack passes arrays between frameworks
zero-copy where possible [374, 375]. Its stated aim: faster to write than CUDA, more flexible than other DSLs [376].

**Costs.** Tiles cannot be freely sliced [377]; scheduling across the chip stays with the programmer [378, 379]. At
its 2021 release it ran only on NVIDIA GPUs [380].

**For Omega.** The tile model for Omegese GPU words (decision 34), with work across the chip scheduled by the motor's
static table (docs/OMEGA.md section 6).

#### Mojo

**Built for.** One language for CPUs, GPUs and accelerators from many vendors, replacing the pile of Python, C++ and
CUDA [381]. Modular rejected embedded languages like Triton for tooling and grammar limits [382]. Mojo sits directly on
MLIR [383, 384], with one owner per value [385] and a lifetime checker [386].

**What changed.** In early 2025 the FAQ said Mojo would grow into a Python superset [387]; the current roadmap says it
may or may not, and that this is acceptable [388]; performance comes first [389]. It was closed until 2026 [390] and is
now open source under Apache 2.0 [391]. Its GPU kernels are still written per thread [392].

**How it talks.** Mojo calls Python by loading the ordinary CPython interpreter [393]; Python reaches Mojo only through
declared bindings [394]; C works through the C ABI [395], C++ only through `extern "C"` [396]. AIEN removed its own Mojo lane
on 2026-10-15 (docs/polyglot/OMEGA_POLYGLOT_CURRENT_STATE.md).

**For Omega.** Being a superset of Python was given up by the team that tried hardest (decision 40). Omega reaches
Python the way Python reaches everything: through the C ABI.

#### Array languages, briefly

Iverson's Turing lecture argued for arrays and operators that let a whole algorithm be one short expression [397].
NumPy's authors say its memory model cannot use GPUs directly, which is why every deep-learning framework built its
own array type [398]. For Omega: arrays as values with shape (decision 17), and the GPU in the same language.
## Part 2. The lessons in one table

Each row: the lesson, the languages that show it, the evidence, and what Omega does, as a numbered decision in the
same format as decisions 16 to 25. Every decision from 26 on is PROPOSED until Drake accepts it (docs/PROGRAM.md,
"What Drake does"). "Already true" means Omega follows the lesson today; the gate named is the existing test that
proves it, with its file. "New" means nothing enforces it yet; the gate named is the one to build.

| # | lesson | languages that show it | evidence | Omega's decision, and why | gate |
|---|---|---|---|---|---|
| 26 | every language meets every other at the platform C ABI; language-specific binary interfaces have cost years | Haskell, Rust, C++, Swift, Zig, Mojo, Node, Lua, Java | Haskell standardised on C as the common base [119, 120]; Rust promises no stable ABI and meets others at C [285, 286]; a stable C++ ABI is hard [81]; Swift projects were locked to one compiler version until source compatibility and the version 5 ABI [295, 296]; Node add-ons rebuilt each version until Node-API [233]; Lua 4.0's interface change stranded its birthplace [204]; JNI is brittle [220] | **Yes.** The C ABI is the only binary promise Omega makes to outside programs. omc writes ELF shared objects that follow it exactly; no Omega-specific binary interface is offered to other languages. Every major language already speaks it (COMPATIBILITY.md section 5) | New: COMPATIBILITY.md rungs C1 to C5 |
| 27 | a language that cannot remove a mistake keeps it forever; a language that breaks everything at once splits its users | Python, Perl, PHP, JavaScript, C++, Lua, Rust, Go | Python's deliberate 3.0 break took until 2020 to finish [175, 178, 179]; Perl 6 took fifteen years and then a new name [144, 147]; PHP 6 abandoned [242]; JavaScript "don't break the Web" [230, 231]; C++ never removes anything [77]; Lua 4.0 [204]; Rust editions coexist [275, 276]; Go 1 programs keep compiling [261] | **Yes.** Every Omega unit names its language version; old versions stay readable and their receipts keep verifying; an incompatible change is a new version that runs beside the old one, as Rust's editions do. Removal is allowed in a new version, so mistakes do not have to live forever. No "Omega 2" that splits the code base | Already true in part: the IR decoder reads "every historical version 1..OSC_IR_VERSION_MAX" (src/compiler/osc_ir.h:266) and refuses unknown ones (C2-DECODE-VERSION, src/checker2/c2_ir.c:232). New: a gate that compiles one program under two language versions in one build and links them |
| 28 | null references cause crashes; retrofitting a fix leaves the hole underneath | Java, C#, Kotlin, Swift, Go | Hoare's billion-dollar mistake [219]; C#'s nullable references are compile-time only [255]; Kotlin and Swift put absence in the type [317, 292]; Go's nil-interface trap [267] | **Yes, no null.** Omegese has no null word and no reference that can point at nothing; an array name always holds an array (own, edit, view) | Already true: the fixed grammar of `omegese next --machine` (docs/one_layer/OMEGESE.md 6.2) has no null word, checked by the next oracle in `make test-omegese` (mk/omegese.mk:33). New: one refusal test that a null-like word is refused |
| 29 | exceptions that jump across functions cost clarity, and are fatal at a language border | Java, C++, Rust, Lua, Go, Zig, Erlang | checked exceptions contested [213, 214, 215]; C++ builds switch exceptions off [78]; a Rust panic across a C boundary aborts [399, 400]; Lua errors are long jumps [401]; Go stopped error-syntax proposals [268]; Zig has no hidden control flow [301]; Erlang lets a failing process stop and a supervisor recover [126] | **Yes, errors are values.** No exceptions. A trap ends the call with a code from 1 to 14; at the door it becomes the function's return value (COMPATIBILITY.md section 3.4). The cost Go paid (verbose error checks) is paid in tokens by an AI writer, and is measured by the token gate | Already true: trap codes checked (C2-TRAP-CODE, tests/checker2/test_c2_ir.c:833-834); code after a trap refused (`make test-omegese`). New: rung C3 |
| 30 | checks that can be switched off get switched off, and the bugs come back | Algol 60 (Hoare), Ada, C | Hoare's customers urged him not to offer an off switch for bounds checks [29, 30]; Ariane 5's unprotected overflow [96]; C's `restrict` is an unchecked promise [402] | **Yes, no off switch.** No flag, mode or build option turns off a bounds check, an overflow trap or an overlap proof. A check leaves the compiled code only when the checker proves it can never fire (decision 23), and the receipt records the proof | Already true for the native path: every array access goes through `bounds_r` (src/compiler/osc_cg.c:182). New: a gate that greps omc's options for any flag that disables a check and finds none |
| 31 | a foreign door left out of the language definition becomes one door per compiler | Standard ML, Haskell | the Standard ML definition said nothing about foreign code, so such programs do not move between compilers [103]; Haskell's standard foreign interface had to settle on C as the lowest common denominator [119] | **Yes.** The C-ABI door, its rules and the interface description are part of the Omega specification, not a feature of omc | New: COMPATIBILITY.md rung C0 (the rules table in the spec) |
| 32 | optional features and dialects mean nobody can rely on anything | Lisp, BASIC, OpenCL, Lua, Ada | Lisp's dialects [16]; BASIC's incompatible implementations [44, 45]; OpenCL 3.0 made everything after 1.2 optional [346, 347]; Lua's missing module policy [203]; Ada validated every compiler and refused subsets [87] | **Yes, one specification, no optional parts.** A back end that cannot do a feature refuses the program with a named error; it never accepts a dialect. checker2 is the one judge for every compiler, seed-born or C-built | Already true: checker2 refuses by named class and issues receipts (src/checker2); the handover gate in docs/PROGRAM.md requires two compilers to agree bit for bit |
| 33 | handing source text to the machine's driver multiplies compilers and bugs; a small checked binary form travels | GLSL, SPIR-V, HLSL, CUDA, Java, Pascal, Julia | hundreds of driver bugs from shader text [351, 352, 354]; SPIR-V and DXIL as binary contracts [355, 356, 362]; PTX stable across generations [328]; Java bytecode [208]; Pascal-P [48]; Julia's compile-on-first-use delays [313] | **Yes, already true.** What reaches the machine is Omega's checked binary IR, compiled ahead of time and signed by a receipt before it runs. No source text and no compiler inside a driver or at run time | Already true: strict IR decoder with mutant tests (`make test-osc-ir-decode`, Makefile:112; refusal classes C2-DECODE-*) |
| 34 | per-thread GPU code is hard to write well; tile code moves the hard parts into the compiler | CUDA, Triton, Mojo, APL | NVIDIA added a tile mode where the thread index disappears [336, 337], which we read as a sign that per-thread code is hard to write well; Triton's compiler handles coalescing, shared memory and tensor cores [369, 370]; Mojo is still per thread [392]; Iverson on whole-array notation [397] | **Yes.** Omegese GPU words (bar C1 of docs/OMEGA.md) are written per tile; omc owns thread indices, coalescing and shared memory. The costs Triton names stay visible: tiles of fixed shape [377], and scheduling across the chip, which Omega gives to the motor's static schedule | New: bar C1 kernels written with no thread-index word; checker proves tile bounds |
| 35 | languages spread through their libraries and the array formats they share | CUDA, PHP, Python, NumPy, Triton | cuDNN and cuBLAS carried CUDA into AI [331, 332]; PHP won on its ecosystem [237, 239]; NumPy and the buffer protocol pulled science onto Python [163, 166]; DLPack passes arrays between frameworks, zero-copy where possible [403, 374, 375] | **Yes.** Omega's libraries (GEMM, attention, reductions) are part of the deliverable, and arrays cross the door in DLPack's layout so every array library can hand them over (COMPATIBILITY.md section 3.3) | New: bar C3 of docs/OMEGA.md; rung C6 |
| 36 | packaging that arrives late or loose costs a decade | Haskell, Lua, Python, Go, Rust | Haskell's package system began in 2004 and took more than fifteen years to become important [116, 117]; Lua had no module policy [203]; Python needed PEP 517 to end one gatekeeper for every build system [174]; Go's minimum version selection [260]; Cargo builds and fetches in one tool [277] | **Yes.** One tool builds and fetches; a dependency is named by the digest of its checked unit (decision 21), so there is no version guessing at all | Already true in design: decision 21's gate, imports resolve by digest (OSC-VC1). New: the tool |
| 37 | a global lock or shared mutable memory caps parallel speed; isolation scales | Python, Ruby, OCaml, Erlang, Go | Python's GIL blocks multi-core use, and removing it costs single-thread speed and binary compatibility [169, 170, 171]; Ruby gives each Ractor its own lock [188]; OCaml removed its global lock only in 2022 [107]; Erlang's share-nothing processes [123, 125]; Go drew users with concurrency [262] | **Yes.** No global lock. Parallel work shares no writable memory unless the checker proves the parts disjoint (decision 18 applied to threads); at the door each thread brings its own workspace (COMPATIBILITY.md section 4) | Already true for calls: C2-CALL-ALIAS (src/checker2/c2_check.c:338). New: rung C7 |
| 38 | compile time is a design limit; features that multiply it are paid for daily | Go, Rust, Kotlin, Julia, C++ | Go's import rules cut build work [259]; Rust's cross-crate specialisation and slow compiles [280, 281]; Kotlin's rule that compiling must not get any slower than with Java's compiler [316]; Julia's latency [313, 314] | **Yes.** omc's compile time and output size are published for every rung (L408's acceptance numbers), and no feature whose compile cost grows faster than program size is added without a measured need | Already true for L408: the omc words-and-compile-time table its brief requires. New: the same table on every later rung |
| 39 | generics are hard to add late and hard to add well | Go, Java, C#, C++ | Go considered designs through its first decade [263, 265, 266]; Java erased types for compatibility [216, 218]; C# specialised them in the runtime [249]; C++ exposes its libraries through C, Java or C# interfaces because stable C++ binaries are hard [82] | **No generics now.** If a measured need appears, they are specialised per type at check time (as C# does for value types), never erased, and never cross the door: exports are always concrete | New: the door refuses a non-concrete export (rung C1) |
| 40 | being a superset of an older language buys users and costs freedom | C++, Mojo, Kotlin | C++'s C base: adoption [72, 73] and complexity [74, 75]; Mojo's Python-superset goal relaxed [387, 388]; Kotlin chose two-way interop over supersetting Java [316, 212] | **No superset.** Omega does not copy Python's or C's syntax or meaning. Compatibility comes through the door and through translators checked by identical output (COMPATIBILITY.md sections 5 and 6) | New: translator gates T1 to T4 |
| 41 | an unsafe escape hatch becomes where the bugs live | Rust, C#, c2rust | Rust's `unsafe` exists because the checker is conservative [283]; C# unsafe code is unverifiable [256]; c2rust's output is unsafe on purpose [404] | **No unsafe form.** Omegese has no unchecked word. What the checker cannot prove is refused, and the AI writer rewrites it; code that truly needs the hardware without proof lives below the floor (rung F of docs/PROGRAM.md) or outside the door | Already true: every IR op is checked by checker2; there is no unchecked op class. New: a grep gate over the spec for an unsafe word |
| 42 | hidden control flow and hidden allocation make code mean something other than it says | Zig, C++ | Zig: no hidden control flow or allocations [301, 302, 303]; C++ exceptions and RTTI switched off for cost [78] | **Yes.** No operator overloading, no hidden calls, no implicit allocation: every word in Omegese does what the specification table says and nothing else | Already true: the fixed word table (docs/one_layer/OMEGESE.md section 3) and the next oracle |
| 43 | the first index and the array order are a border problem, not a style question | Fortran, Julia, Lua, C | Fortran is column-major and counts from 1 [405]; Julia counts from 1 [315]; Lua's 1 is defended [205] | **Zero-based, and explicit at the border.** Omegese counts from 0 (`for` runs 0 to count minus 1); arrays that cross the door carry their shape and order in the descriptor, so a Fortran or Julia caller cannot be misread | Already true inside (docs/one_layer/OMEGESE.md section 3). New: rung C6 includes a column-major Fortran caller |
| 44 | type inference with written signatures catches errors without burying code in types | ML, Haskell, Swift, Rust | Milner's checker trapped many errors while inferring types [97, 98]; inference spread to Rust and others [99, 100] | **Yes, already true.** Omegese infers value types bottom-up; only parameters, returns, conversions and new arrays name a type (Omegese rule 2) | Already true: `make test-omegese` (coverage programs and refusals) |
| 45 | a language that needs no runtime can live inside any host | Lua, Zig, C | Lua is a library with no main and no global state [199, 197]; Zig's standard library works on the freestanding target, with no operating system [303] | **Yes.** Omega output needs no runtime library and no global state outside its declared workspace; it loads as a shared object into any program (COMPATIBILITY.md) | New: rung C1 checks the shared object has no library dependencies |

### Where a lesson pulls against decisions 16 to 24

- **Garbage collection (Lisp, Java) against decision 24.** Lisp's and Java's collectors lasted and removed whole
  classes of memory bugs [11, 209]. Decision 24 stands. Collectors cost the most exactly at the border Omega must
  cross: Go forbids C from keeping a Go pointer [406], Julia callers must pin objects by hand [407], OCaml's C stubs
  need special macros [105], and OCaml carried a global collector lock until 2022 [107]. Python's C extensions must track reference ownership by hand [172]. Rust shows memory
  safety without a collector at scale [273]. Omega's workload is fixed and known before it runs, so allocation can be
  decided at check time.
- **Rust's conservative checker against decision 18.** Rust says its compiler rejects some valid programs and offers
  `unsafe` as the way out [283]. Decision 18 stands, without the way out (decision 41). The writer is an AI that can
  rewrite a refused program at once, and every refusal is counted (the translator gate T3 in COMPATIBILITY.md
  publishes the count). If refusals block real programs, the fix is a stronger proof in checker2, never an escape.
- **Hoare's run-time checks against decision 23.** Hoare argues run-time checks matter most in production runs
  [29, 30]; decision 23 removes the bounds check from the per-token GPU path. Both stand: decision 23 removes a check
  only where the checker proves it can never fire, and decision 30 forbids any other way of removing one. Hoare's
  complaint was about an off switch, which Omega does not have.
- **Python's dynamic glue against decision 21.** Python became the language that calls everything by being dynamic
  and loading extensions at run time [152, 155, 158]. Decision 21 (one unit, imports by digest, no textual inclusion) stands
  inside Omega. Python's reach is copied at the border instead: Omega is callable from Python through the C ABI, so
  Python stays the glue and Omega becomes one more thing it glues.
- **Compile on first use (Julia) against receipts.** Julia gets speed by compiling at run time [313]. Omega cannot:
  a receipt signs checked code before it runs. Decision 33 keeps compilation ahead of time.

### Lessons Omega already follows, and the gate that proves each

| lesson | where Omega follows it | existing gate |
|---|---|---|
| no null (28) | no null word in Omegese | next oracle over the fixed grammar, `make test-omegese` (mk/omegese.mk:33) |
| errors are values, traps carry codes (29) | trap codes 1 to 14 | C2-TRAP-CODE cases in tests/checker2/test_c2_ir.c:833-834 |
| no off switch for bounds checks (30) | every native array access is checked | `bounds_r` on the native path (src/compiler/osc_cg.c:182) |
| checked binary IR, not text (33) | strict canonical decoder | `make test-osc-ir-decode` with mutants (Makefile:112) |
| old versions stay readable (27, in part) | decoder reads versions 1 to 5 | src/compiler/osc_ir.h:266; C2-DECODE-VERSION (src/checker2/c2_ir.c:232) |
| no aliasing at calls (18, 37) | a register passed twice and written is refused | C2-CALL-ALIAS (src/checker2/c2_check.c:338; case at tests/checker2/test_c2_ir.c:845) |
| ownership without a collector (24) | own, edit and view arrays; leaks, double frees and use after move refused | C2-OWN-LEAK and the 12 ownership refusals in `make test-omegese` |
| no implicit conversions (20) | conversions are written words | C2-TYPE-CAST (tests/checker2/test_c2_ir.c:816); two bare numbers in one op refused (`make test-omegese`) |
| loops proven to end | every loop has a bound | C2-LOOP-UNBOUNDED negative control in `make test-omegese` |
| inferred types, written signatures (44) | Omegese rule 2 | `make test-omegese` coverage programs |
| one specification, one judge (32) | checker2 issues every receipt | checker2's named refusal classes; the handover gate in docs/PROGRAM.md |

## What this study does not cover

- Measured costs for most of the "wrong" items. Designers name the mistake; few give a number. Where a number exists
  (Android's vulnerability share, Y2K spending, PHP 7's speed-up) it is quoted with its source.
- Languages outside the brief's list (TypeScript, Scala, Elixir, Dart, Fortran's successors and others).
- The HOPL papers that refused the fetch: Sammet on COBOL, Naur and Perlis on Algol 60, Kurtz on BASIC, Wirth's
  "Recollections" on Pascal. Nearest designer sources are used and named in each section.

## Sources

Every source below was fetched on 2026-10-22 (UTC) and its quoted sentence re-found in the fetched text by a script (re-fetch, strip markup, compare words). Format: [n] URL, title, date, the sentence copied from the page.

[1] https://www.softwarepreservation.org/projects/FORTRAN/paper/p25-backus.pdf, "The History of FORTRAN I, II, and III (Backus, HOPL-I)", 1978. Quote: "The cost of programmers associated with a computer center was usually at least as great as the cost of the computer itself."

[2] https://www.softwarepreservation.org/projects/FORTRAN/paper/p25-backus.pdf, "The History of FORTRAN I, II, and III (Backus, HOPL-I)", 1978. Quote: "To this day I believe that our emphasis on object program efficiency rather than on language design was basically correct."

[3] https://www.softwarepreservation.org/projects/FORTRAN/paper/p25-backus.pdf, "The History of FORTRAN I, II, and III (Backus, HOPL-I)", 1978. Quote: "more than half the machine instructions for these machines are being produced by FORTRAN."

[4] https://flang.llvm.org/docs/Aliasing.html, "Aliasing in Fortran (The Flang Compiler)", n.d. Quote: "A Fortran compiler is free to assume that a program conforms with this rule when optimizing; and while obvious violations should of course be diagnosed, the programmer bears the responsibility to understand and comply with this rule."

[5] https://flang.llvm.org/docs/Aliasing.html, "Aliasing in Fortran (The Flang Compiler)", n.d. Quote: "This prohibition applies to programs (or programmers) and has been in place since Fortran acquired subroutines and functions in Fortran II."

[6] https://www.softwarepreservation.org/projects/FORTRAN/paper/p25-backus.pdf, "The History of FORTRAN I, II, and III (Backus, HOPL-I)", 1978. Quote: "Better facilities were added not long after distribution and fairly good syntactic checking was provided in FORTRAN II."

[7] https://www.cs.cmu.edu/~crary/819-f09/Backus78.pdf, "Can Programming Be Liberated from the von Neumann Style? (Turing Award Lecture)", 1978. Quote: "Conventional programming languages are growing ever more enormous, but not stronger."

[8] https://www.cs.utexas.edu/~EWD/transcriptions/EWD04xx/EWD498.html, "How do we tell truths that might hurt? (EWD498)", 18 June 1975. Quote: "Today they stick to FORTRAN, so that they can share each other's programs, bugs included."

[9] https://snapshots.sourceware.org/gcc/docs/latest/gfortran/Interoperability-with-C.html, "Interoperability with C (The GNU Fortran Compiler)", n.d. Quote: "Since Fortran 2003 (ISO/IEC 1539-1:2004(E)) there is a standardized way to generate procedure and derived-type declarations and global variables that are interoperable with C"

[10] https://snapshots.sourceware.org/gcc/docs/latest/gfortran/Interoperability-with-C.html, "Interoperability with C (The GNU Fortran Compiler)", n.d. Quote: "For instance, neither C's unsigned integers nor C's functions with variable number of arguments have an equivalent in Fortran."

[11] http://jmc.stanford.edu/articles/lisp/lisp.pdf, "History of Lisp (McCarthy)", 12 February 1979. Quote: "Since there were only six bits left in a word, and these were in separated parts of the word, reference counts seemed infeasible without a drastic change in the way list structures were represented."

[12] http://jmc.stanford.edu/articles/lisp/lisp.pdf, "History of Lisp (McCarthy)", 12 February 1979. Quote: "Once the eval interpreter was programmed, it became available to the programmer, and it was especially easy to use because it interprets LISP programs expressed as LISP data."

[13] http://jmc.stanford.edu/articles/lisp/lisp.pdf, "History of Lisp (McCarthy)", 12 February 1979. Quote: "S.R. Russell noticed that eval could serve as an interpreter for LISP, promptly hand coded it, and we now had a programming language with an interpreter."

[14] http://jmc.stanford.edu/articles/lisp/lisp.pdf, "History of Lisp (McCarthy)", 12 February 1979. Quote: "but in all the early LISPs, numerical computations were still 10 to 100 times slower than in FORTRAN."

[15] http://jmc.stanford.edu/articles/lisp/lisp.pdf, "History of Lisp (McCarthy)", 12 February 1979. Quote: "The project of defining M-expressions precisely and compiling them or at least translating them into S-expressions was neither finalized nor explicitly abandoned."

[16] http://jmc.stanford.edu/articles/lisp/lisp.pdf, "History of Lisp (McCarthy)", 12 February 1979. Quote: "there has never been an attempt at agreeing on a standardization"

[17] https://nvlpubs.nist.gov/nistpubs/Legacy/hb/nbshandbook106.pdf, "CODASYL COBOL Journal of Development 1968 (NBS Handbook 106)", July 1969. Quote: "A COmmon Business Oriented Language, independent of any make or model of computer, open ended, and stated in both an English notation and a narrative form, would do much to solve or to ameliorate this problem."

[18] https://nvlpubs.nist.gov/nistpubs/Legacy/hb/nbshandbook106.pdf, "CODASYL COBOL Journal of Development 1968 (NBS Handbook 106)", July 1969. Quote: "Because of the narrative nature and English-like syntax of the language, however, a certain amount of documentation is inherent."

[19] https://ftp.mirrorservice.org/sites/www.bitsavers.org/pdf/ibm/generalInfo/F28-8053-2_COBOL_General_Information_1961.pdf, "COBOL General Information (IBM F28-8053-2)", 1961. Quote: "Programs written in COBOL for one data processing system are readily adaptable to any other system for which a COBOL processor is available."

[20] https://ethw.org/Oral-History:Jean_Sammet, "Oral-History: Jean Sammet (IEEE History Center interview, Janet Abbate)", 29 August 2001. Quote: "every manufacturer had to implement it, essentially because the Department of Defense issued an edict"

[21] https://www.techmonitor.ai/?p=152852, ""IT'S THE END OF THE WORLD, AND WE KNOW IT" (PART I) (Tech Monitor)", 14 February 1996. Quote: "When asked if there were application or database files that do indeed have only 2-digit years, an astonishing 85% admitted that was so."

[22] https://clintonwhitehouse4.archives.gov/non-html/media/pdf/11thy2k.pdf, "11th Quarterly Report, Progress on Year 2000 Conversion (US Office of Management and Budget)", 14 December 1999. Quote: "Agencies now estimate they will spend $8.38 billion fixing the problem from Fiscal Year 1996 through Fiscal Year 2000, an increase from $8.34 billion in September 1999."

[23] https://ethw.org/Oral-History:Jean_Sammet, "Oral-History: Jean Sammet (IEEE History Center interview, Janet Abbate)", 29 August 2001. Quote: "the committee, which I was long since disinvolved with, took them out, on the grounds that nobody had implemented them."

[24] https://www.cs.utexas.edu/~EWD/transcriptions/EWD04xx/EWD498.html, "How do we tell truths that might hurt? (EWD498)", 18 June 1975. Quote: "The use of COBOL cripples the mind; its teaching should, therefore, be regarded as a criminal offence."

[25] https://cseweb.ucsd.edu/~goguen/courses/130/algol60.html, "Revised Report on the Algorithmic Language Algol 60", January 1963. Quote: "The syntax will be described with the aid of metalinguistic formulae"

[26] https://informatika-21.ru/wirth/Pascal50E.pdf, "50 Years of Pascal (Niklaus Wirth)", n.d. Quote: "It was the first time that a language was defined by concisely formulated constructs and by a precise, formal syntax."

[27] https://cseweb.ucsd.edu/~goguen/courses/130/algol60.html, "Revised Report on the Algorithmic Language Algol 60", January 1963. Quote: "Every block automatically introduces a new level of nomenclature."

[28] https://www.labouseur.com/projects/codeReckon/papers/The-Emperors-Old-Clothes.pdf, "The Emperor's Old Clothes (Hoare, Turing Award Lecture, copy hosted by labouseur.com)", February 1981. Quote: "Without the ALGOL 60 concept of recursion, at that time highly controversial, we could not have written this compiler at all."

[29] https://www.labouseur.com/projects/codeReckon/papers/The-Emperors-Old-Clothes.pdf, "The Emperor's Old Clothes (Hoare, Turing Award Lecture, copy hosted by labouseur.com)", February 1981. Quote: "Many years later we asked our customers whether they wished us to provide an option to switch off these checks in the interests of efficiency on production runs. Unanimously, they urged us not"

[30] https://www.labouseur.com/projects/codeReckon/papers/The-Emperors-Old-Clothes.pdf, "The Emperor's Old Clothes (Hoare, Turing Award Lecture, copy hosted by labouseur.com)", February 1981. Quote: "I note with fear and horror that even in 1980 language designers and users have not learned this lesson."

[31] https://cseweb.ucsd.edu/~goguen/courses/130/algol60.html, "Revised Report on the Algorithmic Language Algol 60", January 1963. Quote: "Any formal parameter not quoted in the value list is replaced, throughout the procedure body, by the corresponding actual parameter, after enclosing this latter in parentheses wherever syntactically possible."

[32] https://algol68-lang.org/docs/lindsey-history-of-algol68.pdf, "A History of ALGOL 68 (Lindsey, HOPL-II)", 1996. Quote: "call-by-name (with which is associated Jensen's Device, and which is nowadays regarded as obsolete)"

[33] https://cseweb.ucsd.edu/~goguen/courses/130/algol60.html, "Revised Report on the Algorithmic Language Algol 60", January 1963. Quote: "Each of these is a condensation of the reference language enforced by the limited number of characters on the standard input equipment."

[34] https://algol68-lang.org/docs/lindsey-history-of-algol68.pdf, "A History of ALGOL 68 (Lindsey, HOPL-II)", 1996. Quote: "Indeed, the rigorous application of the principle of "orthogonality" makes it one of the cleanest languages around, as I hope to show."

[35] https://algol68-lang.org/docs/lindsey-history-of-algol68.pdf, "A History of ALGOL 68 (Lindsey, HOPL-II), Appendix B", 1996. Quote: "We regard the effort as an experiment and professional honesty compels us to state that in our considered opinion we judge the experiment to be a failure in both respects."

[36] https://algol68-lang.org/docs/lindsey-history-of-algol68.pdf, "A History of ALGOL 68 (Lindsey, HOPL-II), Appendix B", 1996. Quote: "This forces upon us the conclusion that, regarded as a programming tool, the language must be regarded as obsolete."

[37] https://algol68-lang.org/docs/lindsey-history-of-algol68.pdf, "A History of ALGOL 68 (Lindsey, HOPL-II)", 1996. Quote: "The sad fact is that the great majority of implementations that were started were never completed."

[38] https://informatika-21.ru/wirth/Pascal50E.pdf, "50 Years of Pascal (Niklaus Wirth)", n.d. Quote: "The milestone Algol 68 did appear and then sank quickly into obscurity under its own weight, although a few of its concepts did survive into subsequent languages."

[39] https://algol68-lang.org/docs/lindsey-history-of-algol68.pdf, "A History of ALGOL 68 (Lindsey, HOPL-II)", 1996. Quote: "but the flex facility itself was a mistake"

[40] https://algol68-lang.org/docs/lindsey-history-of-algol68.pdf, "A History of ALGOL 68 (Lindsey, HOPL-II)", 1996. Quote: "I now think this was a mistake, since to have such preconditions (even if all they trigger is a run-time check) can simplify writing the body of a routine."

[41] https://archive.org/download/BackToBASIC-KemenyKurtz/BackToBASIC-KemenyKurtz_djvu.txt, "Back to BASIC: The History, Corruption, and Future of the Language (Kemeny and Kurtz, archive.org text)", 1985. Quote: "The second was that we needed to develop a new language, one that was far easier to learn and use for large numbers of students; none of the existing languages was suitable."

[42] https://archive.org/download/BackToBASIC-KemenyKurtz/BackToBASIC-KemenyKurtz_djvu.txt, "Back to BASIC: The History, Corruption, and Future of the Language (Kemeny and Kurtz, archive.org text)", 1985. Quote: "To correct a line you retyped it, using the same line number."

[43] https://archive.org/download/BackToBASIC-KemenyKurtz/BackToBASIC-KemenyKurtz_djvu.txt, "Back to BASIC: The History, Corruption, and Future of the Language (Kemeny and Kurtz, archive.org text)", 1985. Quote: "The GOTO, IF THEN, and GOSUB referred to line numbers only."

[44] https://archive.org/download/BackToBASIC-KemenyKurtz/BackToBASIC-KemenyKurtz_djvu.txt, "Back to BASIC: The History, Corruption, and Future of the Language (Kemeny and Kurtz, archive.org text)", 1985. Quote: "Some of these compromises were ugly and violated our design principles for the language."

[45] https://archive.org/download/BackToBASIC-KemenyKurtz/BackToBASIC-KemenyKurtz_djvu.txt, "Back to BASIC: The History, Corruption, and Future of the Language (Kemeny and Kurtz, archive.org text)", 1985. Quote: "The result is that no two of these implementations are compatible."

[46] https://www.cs.utexas.edu/~EWD/transcriptions/EWD04xx/EWD498.html, "How do we tell truths that might hurt? (EWD498)", 18 June 1975. Quote: "It is practically impossible to teach good programming to students that have had a prior exposure to BASIC: as potential programmers they are mentally mutilated beyond hope of regeneration."

[47] https://informatika-21.ru/wirth/Pascal50E.pdf, "50 Years of Pascal (Niklaus Wirth)", n.d. Quote: "Most essential was the pervasive concepts of data types: Every constant, variable, or function was of a fixed, static type."

[48] https://informatika-21.ru/wirth/Pascal50E.pdf, "50 Years of Pascal (Niklaus Wirth)", n.d. Quote: "It was my idea to postulate a hypothetical computer, which would be simple to realize on various other main frames, and for which we would build a Pascal compiler at ETH."

[49] https://informatika-21.ru/wirth/Pascal50E.pdf, "50 Years of Pascal (Niklaus Wirth)", n.d. Quote: "A group under Ken Bowles at the University of San Diego, and Philippe Kahn at Borland Inc."

[50] https://www.cs.virginia.edu/~evans/cs655/readings/bwk-on-pascal.html, "Why Pascal is Not My Favorite Programming Language (Kernighan)", 2 April 1981. Quote: "Because arr10 and arr20 have different types, it is not possible to write a single procedure that will sort them both."

[51] https://www.cs.virginia.edu/~evans/cs655/readings/bwk-on-pascal.html, "Why Pascal is Not My Favorite Programming Language (Kernighan)", 2 April 1981. Quote: "But in its standard form (both current and proposed), Pascal is not adequate for writing real programs."

[52] https://www.cs.virginia.edu/~evans/cs655/readings/bwk-on-pascal.html, "Why Pascal is Not My Favorite Programming Language (Kernighan)", 2 April 1981. Quote: "It is suitable only for small, self-contained programs that have only trivial interactions with their environment and that make no use of any software written by anyone else."

[53] https://informatika-21.ru/wirth/Pascal50E.pdf, "50 Years of Pascal (Niklaus Wirth)", n.d. Quote: "we found it impossible to translate a program written in a structured language into an unstructured language"

[54] https://www.cs.tufts.edu/~nr/cs257/archive/kristen-nygaard/hopl-simula.pdf, "The Development of the SIMULA Languages (HOPL I)", 1978 (published 1981). Quote: "It should be unifying, pointing out similarities and differences between various kinds of network systems."

[55] https://www.cs.tufts.edu/~nr/cs257/archive/kristen-nygaard/hopl-simula.pdf, "The Development of the SIMULA Languages (HOPL I)", 1978 (published 1981). Quote: "our work to resolve these problems resulted in the class/subclass concepts which structured the rest of the new language."

[56] https://www.stroustrup.com/hopl2.pdf, "A History of C++: 1979-1991 (Stroustrup, HOPL II)", 1993. Quote: "Simula’s class concept was seen as the key difference and ever since I have seen classes as the proper primary focus of program design."

[57] https://www.cs.tufts.edu/~nr/cs257/archive/kristen-nygaard/hopl-simula.pdf, "The Development of the SIMULA Languages (HOPL I)", 1978 (published 1981). Quote: "It was crucial for the continuation of the whole SIMULA 67 effort to get at least one major manufacturer (in this case Control Data, see Section 3.5) to commit itself to implementations."

[58] https://www.cs.tufts.edu/~nr/cs257/archive/kristen-nygaard/hopl-simula.pdf, "The Development of the SIMULA Languages (HOPL I)", 1978 (published 1981). Quote: "Unfortunately we chose to model the virtual specifications after the parameter specifications of ALGOL, which meant that the parameters of a virtual procedure had to be run time checked."

[59] https://www.cs.tufts.edu/~nr/cs257/archive/kristen-nygaard/hopl-simula.pdf, "The Development of the SIMULA Languages (HOPL I)", 1978 (published 1981). Quote: "Automatic data retention was felt to be a fairly dangerous approach"

[60] https://www.stroustrup.com/hopl2.pdf, "A History of C++: 1979-1991", 1993. Quote: "The overhead problems were fundamental to Simula and could not be remedied."

[61] http://worrydream.com/EarlyHistoryOfSmalltalk/, "The Early History of Smalltalk (Alan Kay, HOPL II, copy on worrydream.com)", 1993. Quote: "The mental image was one of separate computers sending requests to other computers that had to be accepted and understood by the receivers before anything could happen."

[62] http://lists.squeakfoundation.org/pipermail/squeak-dev/1998-October/017019.html, "prototypes vs classes was: Re: Sun's HotSpot (Alan Kay, squeak-dev mailing list)", 1998-10. Quote: "Smalltalk is not only NOT its syntax or the class library, it is not even about classes."

[63] http://lists.squeakfoundation.org/pipermail/squeak-dev/1998-October/017019.html, "prototypes vs classes was: Re: Sun's HotSpot (Alan Kay, squeak-dev mailing list)", 1998-10. Quote: "that is what the kernal of Smalltalk/Squeak is all about (and it's something that was never quite completed in our Xerox PARC phase)."

[64] http://worrydream.com/EarlyHistoryOfSmalltalk/, "The Early History of Smalltalk (Alan Kay, HOPL II, copy on worrydream.com)", 1993. Quote: "Now programmers could easily implement the virtual machine without having to reinvent it, and, in several cases, groups were able to roll their own image of basic classes."

[65] https://ftp.gwdg.de/pub/languages/smalltalk/st.cs.uiuc.edu/Smalltalk/Squeak/docs/OOPSLA.Squeak.html, "Back to the Future: The Story of Squeak, A Practical Smalltalk Written in Itself", 1997. Quote: "It also runs bit-identical images across its wide portability base."

[66] https://ftp.gwdg.de/pub/languages/smalltalk/st.cs.uiuc.edu/Smalltalk/Squeak/docs/OOPSLA.Squeak.html, "Back to the Future: The Story of Squeak, A Practical Smalltalk Written in Itself", 1997. Quote: "make changes immediately and without needing to see or deal with any language other than Smalltalk."

[67] https://lists.squeakfoundation.org/pipermail/squeak-dev/2010-May/150654.html, "[squeak-dev] FFI Documentation (community help draft)", 2010-05. Quote: "FFI, the Squeak Foreign Function Interface, is used to call functions located in shared libraries that are not part of the Squeak VM nor its plugins."

[68] https://lists.squeakfoundation.org/pipermail/squeak-dev/2010-May/150654.html, "[squeak-dev] FFI Documentation (community help draft)", 2010-05. Quote: "users are limited to using the functionality you provide and can not call other external functions"

[69] https://www.stroustrup.com/hopl20main-p5-p-bfc9cd4--final.pdf, "Thriving in a Crowded and Changing World: C++ 2006-2020", 2020-06. Quote: "I wanted a tool that combined the ability to deal directly and efficiently with hardware"

[70] https://www.stroustrup.com/hopl-almost-final.pdf, "Evolving a language in and for the real world: C++ 1991-2006", 2007. Quote: "Use traditional (dumb) linkers. No gratuitous incompatibilities with C. Leave no room for a lower-level language below C++ (except assembler)."

[71] https://www.stroustrup.com/hopl2.pdf, "A History of C++: 1979-1991", 1993. Quote: "To wit: Someone once demonstrated a 3% systematic decrease in overall run−time efficiency compared with C."

[72] https://www.stroustrup.com/hopl2.pdf, "A History of C++: 1979-1991", 1993. Quote: "The ten years that followed clearly showed that choosing C as a base left me in the mainstream of systems programming where I intended to be."

[73] https://www.stroustrup.com/hopl20main-p5-p-bfc9cd4--final.pdf, "Thriving in a Crowded and Changing World: C++ 2006-2020", 2020-06. Quote: "In that period, C++ grew from a one-person research project to a community of about 3 million programmers."

[74] https://www.stroustrup.com/hopl2.pdf, "A History of C++: 1979-1991", 1993. Quote: "The cost in language complexity has been considerable, but manageable."

[75] https://www.stroustrup.com/hopl20main-p5-p-bfc9cd4--final.pdf, "Thriving in a Crowded and Changing World: C++ 2006-2020", 2020-06. Quote: "I implemented auto in the winter of 1982/83 but was forced to remove it to preserve C compatibility."

[76] https://www.stroustrup.com/hopl20main-p5-p-bfc9cd4--final.pdf, "Thriving in a Crowded and Changing World: C++ 2006-2020", 2020-06. Quote: "One way of looking at C++ is as the result of decades of three contradictory demands"

[77] https://www.stroustrup.com/hopl20main-p5-p-bfc9cd4--final.pdf, "Thriving in a Crowded and Changing World: C++ 2006-2020", 2020-06. Quote: "nothing significant ever goes away. Stability is a key feature."

[78] https://www.stroustrup.com/hopl-almost-final.pdf, "Evolving a language in and for the real world: C++ 1991-2006", 2007. Quote: "Implementations aimed at embedded or high performance applications all have compiler options for disabling run-time type identification and exceptions."

[79] https://www.stroustrup.com/hopl2.pdf, "A History of C++: 1979-1991", 1993. Quote: "The feature I regret not having put in much earlier when it could be done without formal debate and experimentation was easy is some form of name space control"

[80] https://www.stroustrup.com/hopl2.pdf, "A History of C++: 1979-1991", 1993. Quote: "Given that the name of a member function includes the name of its class and that the type of an overloaded function has to be reflected in the linkage process somehow or other, I had little choice."

[81] https://www.stroustrup.com/hopl20main-p5-p-bfc9cd4--final.pdf, "Thriving in a Crowded and Changing World: C++ 2006-2020", 2020-06. Quote: "ABI stability for C++ is a genuinely hard technical problem, especially as the C++ standard has to be platform independent."

[82] https://www.stroustrup.com/hopl20main-p5-p-bfc9cd4--final.pdf, "Thriving in a Crowded and Changing World: C++ 2006-2020", 2020-06. Quote: "I consider this a contributing reason that people require C, Java, C#, etc., interfaces to programs written in C++."

[83] http://archive.adaic.com/pol-hist/history/holwg-93/holwg-93.htm, "Ada - The Project: The DoD High Order Language Working Group (William A. Whitaker)", 1993-03. Quote: "When we started there were more than 450 languages in use in the DoD, each with its a compiler, in almost all cases limited to one machine and weapons system."

[84] https://dwheeler.com/steelman/steelman.htm, "Steelman Requirements for High Order Computer Programming Languages", 1978-06. Quote: "The language shall be designed to avoid error prone features and to maximize automatic detection of programming errors."

[85] https://dwheeler.com/steelman/steelman.htm, "Steelman Requirements for High Order Computer Programming Languages", 1978-06. Quote: "The constraints that characterize subtypes shall include range, precision, scale, index ranges, and user defined constraints."

[86] http://archive.adaic.com/pol-hist/history/holwg-93/holwg-93.htm, "Ada - The Project: The DoD High Order Language Working Group (William A. Whitaker)", 1993-03. Quote: "Readability was much more important than writability, a requirement contrary to those advocating the"

[87] http://archive.adaic.com/pol-hist/history/holwg-93/holwg-93.htm, "Ada - The Project: The DoD High Order Language Working Group (William A. Whitaker)", 1993-03. Quote: "This path drives other requirements and properties, like machine independence which forces the validation requirement and the rejection of subsets."

[88] https://adacore.com/sparkpro, "AdaCore SPARK Pro", n.d. Quote: "SPARK is a programming language derived from Ada, designed to eliminate undefined behavior and enable formal verification."

[89] https://www.adacore.com/blog/formal-methods-to-improve-code-quality, "Formal Methods To Improve Code Quality (AdaCore blog)", n.d. Quote: "To achieve this feat, the developer will have to put in a bit more work during development."

[90] https://www.adaic.org/resources/add_content/standards/12rm/html/RM-B-3.html, "Ada Reference Manual B.3 Interfacing with C and C++", 2012. Quote: "The package Interfaces.C contains the basic types, constants, and subprograms that allow an Ada program to pass scalars and strings to C and C++ functions."

[91] http://archive.adaic.com/pol-hist/history/holwg-93/holwg-93.htm, "Ada - The Project: The DoD High Order Language Working Group (William A. Whitaker)", 1993-03. Quote: "The cost of compilers and the computer resources they demanded would be offset by the benefits of reliable systems and reduced maintenance."

[92] https://shape-of-code.com/2017/08/25/microcomputers-killed-ada/, "Microcomputers killed Ada (shape of code)", 2017-08-25. Quote: "In the DoD ecosystems, compilers had to be sold for thousands or hundreds of thousands of dollars/pounds because the markets were small."

[93] http://archive.adaic.com/pol-hist/history/holwg-93/holwg-93.htm, "Ada - The Project: The DoD High Order Language Working Group (William A. Whitaker)", 1993-03. Quote: "When we started we were worried about a $3 billion a year DoD problem, today it is a $30 billion expense to the DoD."

[94] http://archive.adaic.com/pol-hist/history/holwg-93/holwg-93.htm, "Ada - The Project: The DoD High Order Language Working Group (William A. Whitaker)", 1993-03. Quote: "I mistakenly thought that with the influence of the DoD we could pull it off."

[95] https://www.route-fifty.com/digital-government/1998/06/its-curtains-for-the-ada-joint-program-office/291645, "It's curtains for the Ada Joint Program Office", 1998-06-22. Quote: "Emmett Paige Jr., then-assistant secretary of Defense for command, control, communications and intelligence, killed a long-standing mandate that DOD programmers use Ada"

[96] https://www-users.cse.umn.edu/~arnold/disasters/ariane5rep.html, "Ariane 5 Flight 501 Failure, Report by the Inquiry Board", 1996-07-19. Quote: "The data conversion instructions (in Ada code) were not protected from causing an Operand Error, although other conversions of comparable variables in the same place in the code were protected."

[97] https://prl.khoury.northeastern.edu/img/m-jcss-1978.pdf, "A Theory of Type Polymorphism in Programming", 1978. Quote: "The compile-time type checker for this language has proved to be a valuable filter which traps a significant proportion of programming errors."

[98] https://www.cs.tufts.edu/~nr/cs257/archive/dave-macqueen/history-of-ml.pdf, "The History of Standard ML", 2020-06. Quote: "A major benefit of this typing algorithm was that it did not require the programmer to explicitly specify types (with rare exceptions), but instead inferred the types implicit in the code."

[99] https://www.cs.tufts.edu/~nr/cs257/archive/dave-macqueen/history-of-ml.pdf, "The History of Standard ML", 2020-06. Quote: "popularized a number of features that functional-language programers take for granted today."

[100] https://doc.rust-lang.org/reference/influences.html, "The Rust Reference: Influences", n.d. Quote: "SML, OCaml: algebraic data types, pattern matching, type inference, semicolon statement separation"

[101] https://caml.inria.fr/about/history.en.html, "A History of OCaml", n.d. Quote: "Despite its slow and resource-hungry nature, and the lack of a built-in system to handle separate compilation, it gained significant popularity."

[102] https://caml.inria.fr/about/history.en.html, "A History of OCaml", n.d. Quote: "Caml Light was highly portable and was able to run even on small desktop machines like the Macs and PCs of that era."

[103] https://www.cs.tufts.edu/~nr/cs257/archive/dave-macqueen/history-of-ml.pdf, "The History of Standard ML", 2020-06. Quote: "Neither the Definition nor the Basis Library addressed the question of interoperability with foreign code."

[104] https://www.cs.tufts.edu/~nr/cs257/archive/dave-macqueen/history-of-ml.pdf, "The History of Standard ML", 2020-06. Quote: "In retrospect, it was probably a mistake to include the standard I/O streams in the TextIO structure."

[105] https://ocaml.org/manual/5.3/intfc.html, "The OCaml Manual, Chapter 22: Interfacing C with OCaml", n.d. Quote: "Unused blocks in the heap are automatically reclaimed by the garbage collector. This requires some cooperation from C code that manipulates heap-allocated blocks."

[106] https://ocaml.org/manual/5.3/intfc.html, "The OCaml Manual, Chapter 22: Interfacing C with OCaml", n.d. Quote: "All those unmentioned headers are part of the internal runtime API, for which there is no stability guarantee."

[107] https://caml.inria.fr/about/history.en.html, "A History of OCaml", n.d. Quote: "The OCaml 5.0 release in 2022 was a major milestone: a complete rewrite of the language runtime, removing the global GC lock and adding effect handlers via delimited continuations."

[108] https://www.microsoft.com/en-us/research/wp-content/uploads/2016/07/history.pdf, "A History of Haskell: Being Lazy with Class", 2007. Quote: "Once we were committed to a lazy language, a pure one was inescapable."

[109] https://www.microsoft.com/en-us/research/wp-content/uploads/2016/07/history.pdf, "A History of Haskell: Being Lazy with Class", 2007. Quote: "perhaps the biggest single benefit of laziness is not laziness per se, but rather that laziness kept us pure"

[110] https://www.microsoft.com/en-us/research/wp-content/uploads/2016/07/history.pdf, "A History of Haskell: Being Lazy with Class", 2007. Quote: "Necessity being the mother of invention, this embarrassment ultimately led to the invention of monadic I/O"

[111] https://www.microsoft.com/en-us/research/wp-content/uploads/2016/07/history.pdf, "A History of Haskell: Being Lazy with Class", 2007. Quote: "Type classes were introduced to the Haskell Committee by Wadler in a message sent to the fplangc mailing list dated 24 February 1988."

[112] https://doc.rust-lang.org/reference/influences.html, "The Rust Reference: Influences", n.d. Quote: "Haskell (GHC): typeclasses, type families Newsqueak, Alef, Limbo: channels, concurrency"

[113] http://nondot.org/sabre/, "Chris Lattner's Homepage", n.d. Quote: "drawing ideas from Objective-C, Rust, Haskell, Ruby, Python, C#, CLU, and far too many others to list."

[114] https://www.microsoft.com/en-us/research/wp-content/uploads/2016/07/history.pdf, "A History of Haskell: Being Lazy with Class", 2007. Quote: "it is very hard for even experienced programmers to predict the space behaviour of lazy programs, and there can be much more than a constant factor at stake."

[115] https://www.microsoft.com/en-us/research/wp-content/uploads/2016/07/history.pdf, "A History of Haskell: Being Lazy with Class", 2007. Quote: "the prevalence of these space leaks led us to add some strict features to Haskell, such as seq and strict data types"

[116] https://www.microsoft.com/en-us/research/wp-content/uploads/2016/07/history.pdf, "A History of Haskell: Being Lazy With Class (HOPL III)", 2007. Quote: "In 2004, Isaac Jones took up the challenge of leading an effort to specify and implement a system called Cabal"

[117] https://www.microsoft.com/en-us/research/wp-content/uploads/2016/07/history.pdf, "A History of Haskell: Being Lazy with Class", 2007. Quote: "it has taken more than fifteen years for Haskell to gain enough momentum that these distribution and discovery mechanisms have become important."

[118] https://www.microsoft.com/en-us/research/wp-content/uploads/2016/07/history.pdf, "A History of Haskell: Being Lazy with Class", 2007. Quote: "The trouble with runaway success, such as that of Java, is that you get too many users, and the language becomes bogged down in standards, user groups, and legacy issues."

[119] https://www.microsoft.com/en-us/research/wp-content/uploads/2016/07/history.pdf, "A History of Haskell: Being Lazy with Class", 2007. Quote: "This so-called Foreign Function Interface (FFI) treats C as a lowest common denominator: once you can call C you can call practically anything else."

[120] https://www.haskell.org/onlinereport/haskell2010/haskellch8.html, "Haskell 2010 Language Report, Chapter 8: Foreign Function Interface", 2010. Quote: "The Haskell FFI currently only specifies the interaction between Haskell code and foreign code that follows the C calling convention."

[121] https://www.labouseur.com/courses/erlang/history-of-erlang-armstrong.pdf, "A History of Erlang (Joe Armstrong, HOPL III, ACM 2007; this URL is a course-page copy of the paper, the ACM page itself returned 403 to us)", 2007. Quote: "the failure of a process or of hardware should only influence the immediate transaction involved and that all other operations in the machine should progress as if no failures had occurred"

[122] https://erlang.org/download/armstrong_thesis_2003.pdf, "Making reliable distributed systems in the presence of software errors (Joe Armstrong, PhD thesis, KTH)", 2003. Quote: "We assume that such programs do contain errors, and investigate methods for building reliable systems despite such errors."

[123] https://www.labouseur.com/courses/erlang/history-of-erlang-armstrong.pdf, "A History of Erlang (Joe Armstrong, HOPL III, ACM 2007; course-page copy)", 2007. Quote: "These processes have no shared memory and communicate by asynchronous message passing."

[124] https://erlang.org/download/armstrong_thesis_2003.pdf, "Making reliable distributed systems in the presence of software errors (Joe Armstrong, PhD thesis, KTH)", 2003. Quote: "Message passing is the only way to pass data between processes."

[125] https://www.labouseur.com/courses/erlang/history-of-erlang-armstrong.pdf, "A History of Erlang (Joe Armstrong, HOPL III, ACM 2007; course-page copy)", 2007. Quote: "copying data between processes increases process isolation, increases concurrency and simplifies the construction of distributed systems"

[126] https://erlang.org/download/armstrong_thesis_2003.pdf, "Making reliable distributed systems in the presence of software errors (Joe Armstrong, PhD thesis, KTH)", 2003. Quote: "By this I mean that in the event of an error, then the program should just crash."

[127] https://www.labouseur.com/courses/erlang/history-of-erlang-armstrong.pdf, "A History of Erlang (Joe Armstrong, HOPL III, ACM 2007; course-page copy)", 2007. Quote: "Erlang allows two versions of the same module to exist at any one time, this is to allow dynamic code-upgrade operations."

[128] https://www.labouseur.com/courses/erlang/history-of-erlang-armstrong.pdf, "A History of Erlang (Joe Armstrong, HOPL III, ACM 2007; course-page copy)", 2007. Quote: "After all, better a slow system that can recover from errors than a fast system that cannot handle failures."

[129] https://www.labouseur.com/courses/erlang/history-of-erlang-armstrong.pdf, "A History of Erlang (Joe Armstrong, HOPL III, ACM 2007; course-page copy)", 2007. Quote: "In order to use Erlang to make a real product, it would need to be at least 40 times faster."

[130] https://www.labouseur.com/courses/erlang/history-of-erlang-armstrong.pdf, "A History of Erlang (Joe Armstrong, HOPL III, ACM 2007; course-page copy)", 2007. Quote: "Other operating system services, like resource allocation and device drivers needed to access the hardware, can easily be written in C and dynamically linked into the Erlang runtime system."

[131] https://www.erlang.org/doc/system/ports.html, "Ports and Port Drivers (Erlang System Documentation)", n.d. Quote: "Ports provide the basic mechanism for communication with the external world, from Erlang's point of view."

[132] https://www.erlang.org/doc/system/ports.html, "Ports and Port Drivers (Erlang System Documentation)", n.d. Quote: "An erroneous port driver causes the entire Erlang runtime system to leak memory, hang, or crash."

[133] https://www.erlang.org/doc/apps/erts/erl_nif.html, "erl_nif (Erlang runtime system documentation)", n.d. Quote: "A native function that crashes will crash the whole VM."

[134] https://www.erlang.org/doc/apps/erts/erl_nif.html, "erl_nif (Erlang runtime system documentation)", n.d. Quote: "The VM cannot provide the same services as provided when executing Erlang code, such as pre-emptive scheduling or memory protection."

[135] https://perldoc.perl.org/perlintro, "perlintro (Perl documentation)", n.d. Quote: "Perl is a general-purpose programming language originally developed for text manipulation and now used for a wide range of tasks including system administration, web development, network programming, GUI development, and more."

[136] https://perldoc.perl.org/perlintro, "perlintro (Perl documentation)", n.d. Quote: "The language is intended to be practical (easy to use, efficient, complete) rather than beautiful (tiny, elegant, minimal)."

[137] https://perl.org/about.html, "About Perl (perl.org)", n.d. Quote: "There are over 42,000 open source modules available from the Comprehensive Perl Archive Network (CPAN)."

[138] https://web.archive.org/web/20231224152723/http://www.wall.org/~larry/pm.html, "Perl, the first postmodern computer language (Larry Wall; Internet Archive copy, the live page refuses automated fetches)", n.d. Quote: "Personally, I think the Perl slogan, There's More Than One Way To Do It, applies outside of Perl as well as inside."

[139] https://www.perl.com/pub/2007/12/06/soto-11.html, "Programming is Hard, Let's Go Scripting... (Larry Wall, State of the Onion 11)", Dec 6, 2007. Quote: "not just because of the borrowed ideas, but because both Perl and Ruby take their functional programming support rather more seriously"

[140] https://perldoc.perl.org/perlstyle, "perlstyle (Perl documentation)", n.d. Quote: "Perl is designed to give you several ways to do anything, so consider picking the most readable one."

[141] https://perldoc.perl.org/perlstyle, "perlstyle (Perl documentation)", n.d. Quote: "The defaults are there for lazy systems programmers writing one-shot programs. If you want your program to be readable, consider supplying the argument."

[142] https://www.perl.com/pub/2000/10/23/soto2000.html, "State of the Onion 2000 (Larry Wall)", Oct 24, 2000. Quote: "announce to the world that the effort to write Perl 6 has begun in earnest"

[143] https://www.perl.com/pub/2000/10/23/soto2000.html, "State of the Onion 2000 (Larry Wall)", Oct 24, 2000. Quote: "This means that for the first time in history we have the opportunity to make some incompatible fixes to Perl while preserving a migration path for the current code."

[144] https://docs.raku.org/language/faq, "Raku FAQ (docs.raku.org)", n.d. Quote: "version 6.c (Christmas 2015) is the first official release of Raku as a language, along with a validation suite and a compiler that passes it."

[145] https://github.com/Raku/problem-solving/issues/81, ""Perl" in the name "Perl 6" is confusing and irritating (Raku/problem-solving issue 81)", Aug 8, 2019. Quote: "It took way too long to mature to an initial release."

[146] https://github.com/Raku/problem-solving/issues/81, ""Perl" in the name "Perl 6" is confusing and irritating (Raku/problem-solving issue 81)", Aug 8, 2019. Quote: "Having two programming languages that are sufficiently different to not be source compatible, but only differ in what many perceive to be a version number, is hurting the image of both Perl 5 and Perl 6 in the world."

[147] https://github.com/Raku/problem-solving/issues/81, ""Perl" in the name "Perl 6" is confusing and irritating (Raku/problem-solving issue 81)", Oct 14, 2019. Quote: "Since the PR got merged, going to close this issue now."

[148] https://perl.org/about.html, "About Perl (perl.org)", n.d. Quote: "it is a separate language which has its own development team."

[149] https://perl.org/about.html, "About Perl (perl.org)", n.d. Quote: "Perl interfaces with external C/C++ libraries through XS or SWIG."

[150] https://perldoc.perl.org/perlxs, "perlxs, XS Language Reference Manual (Perl documentation)", n.d. Quote: "For creating simple bindings to existing compiled libraries, there is also the libffi interface via CPAN modules like FFI::Platypus or FFI::Raw ."

[151] https://docs.python.org/3/faq/general.html, "General Python FAQ (Why was Python created in the first place?)", n.d. Quote: "We needed a better way to do system administration than by writing either C programs or Bourne shell scripts"

[152] https://docs.python.org/3/faq/general.html, "General Python FAQ (Why was Python created in the first place?)", n.d. Quote: "I realized that it would be foolish to write an Amoeba-specific language, so I decided that I needed a language that was generally extensible."

[153] https://python-history.blogspot.com/2009/01/pythons-design-philosophy.html, "The History of Python: Python's Design Philosophy (Guido van Rossum)", 2009-01-13. Quote: "A large complex system should have multiple levels of extensibility."

[154] https://python-history.blogspot.com/2009/01/pythons-design-philosophy.html, "The History of Python: Python's Design Philosophy (Guido van Rossum)", 2009-01-13. Quote: "fret too much about performance--plan to optimize later when needed"

[155] https://docs.python.org/3/extending/index.html, "Extending and Embedding the Python Interpreter", n.d. Quote: "interact with native libraries or achieve better performance by avoiding the overhead of an interpreter"

[156] https://peps.python.org/pep-0384/, "PEP 384, Defining a Stable ABI", 2009-05-17. Quote: "Extension modules and applications embedding Python can work with different feature releases as long as they restrict themselves to this stable ABI."

[157] https://peps.python.org/pep-0384/, "PEP 384: Defining a Stable ABI", 2009-05-17. Quote: "This PEP proposes to define a stable set of API functions which are guaranteed to be available for the lifetime of Python 3, and which will also remain binary-compatible across versions."

[158] https://docs.python.org/3/library/ctypes.html, "ctypes, A foreign function library for Python", n.d. Quote: "It provides C compatible data types, and allows calling functions in DLLs or shared libraries."

[159] https://cffi.readthedocs.io/en/stable/overview.html, "CFFI documentation, Overview (ABI versus API)", n.d. Quote: "The most immediate drawback of the ABI level is that calling functions needs to go through the very general libffi library, which is slow (and not always perfectly tested on non-standard platforms)."

[160] https://cffi.readthedocs.io/en/stable/overview.html, "CFFI Overview (ABI versus API)", n.d. Quote: "If using a C compiler to install your module is an option, it is highly recommended to use the API mode instead."

[161] https://docs.python.org/3/tutorial/stdlib.html, "The Python Tutorial, 10. Brief tour of the standard library (10.12 Batteries included)", n.d. Quote: "This is best seen through the sophisticated and robust capabilities of its larger packages."

[162] https://peps.python.org/pep-0020/, "PEP 20, The Zen of Python", n.d. Quote: "Flat is better than nested. Sparse is better than dense. Readability counts."

[163] https://peps.python.org/pep-3118/, "PEP 3118, Revising the buffer protocol", 2006-08-28. Quote: "This interface will allow any extension module to either create objects that share memory or create algorithms that use and manipulate raw memory from arbitrary objects that export the interface."

[164] https://peps.python.org/pep-3118/, "PEP 3118: Revising the buffer protocol", 2006-08-28. Quote: "Thus, stride information is the additional information that must be shared."

[165] https://www.nature.com/articles/s41586-020-2649-2, "Array programming with NumPy (Harris et al., Nature)", 2020-09-16. Quote: "NumPy can store arrays in either C or Fortran memory order, iterating first over either rows or columns."

[166] https://www.nature.com/articles/s41586-020-2649-2, "Array programming with NumPy (Harris et al., Nature)", 2020-09-16. Quote: "NumPy underpins almost every Python library that does scientific or numerical computation"

[167] https://packaging.python.org/en/latest/overview/, "Packaging Python Projects, Overview of Python Packaging", n.d. Quote: "Not all developers have the right tools or experiences to build these components written in these compiled languages, so Python created the Wheel, a package format designed to ship libraries with compiled artifacts."

[168] https://docs.python.org/3/glossary.html, "Python glossary, entry "global interpreter lock"", n.d. Quote: "This simplifies the CPython implementation by making the object model (including critical built-in types such as dict) implicitly safe against concurrent access."

[169] https://peps.python.org/pep-0703/, "PEP 703, Making the Global Interpreter Lock Optional in CPython", 2023-01-09. Quote: "The GIL is an obstacle to using multi-core CPUs from Python efficiently."

[170] https://peps.python.org/pep-0703/, "PEP 703, Making the Global Interpreter Lock Optional in CPython", 2023-01-09. Quote: "The changes proposed in the PEP will increase execution overhead for --disable-gil builds compared to Python builds with the GIL."

[171] https://peps.python.org/pep-0703/, "PEP 703, Making the Global Interpreter Lock Optional in CPython", 2023-01-09. Quote: "CPython builds without the GIL will not be ABI compatible with the standard CPython build or with the stable ABI due to changes to the Python object header needed to support biased reference counting."

[172] https://docs.python.org/3/c-api/intro.html, "Python/C API Reference Manual, Introduction (Reference Counts)", n.d. Quote: "When no ownership is transferred, the caller is said to borrow the reference."

[173] https://docs.python.org/3/whatsnew/3.11.html, "What's New In Python 3.11", 2022-10-24. Quote: "Python 3.11 is between 10-60% faster than Python 3.10."

[174] https://peps.python.org/pep-0517/, "PEP 517, A build-system independent format for source trees", 2015-09-30. Quote: "While distutils / setuptools have taken us a long way, they suffer from three serious problems"

[175] https://docs.python.org/3/whatsnew/3.0.html, "What's New In Python 3.0 (Guido van Rossum)", n.d. Quote: "is the first ever intentionally backwards incompatible Python release"

[176] https://www.linuxjournal.com/node/10185, "Interview with Guido van Rossum (Linux Journal)", 2008-10-01. Quote: "This was done so as to maintain backward compatibility with Python 1, but it created a new major ambiguity."

[177] https://www.python.org/doc/sunset-python-2/, "Sunsetting Python 2 (python.org)", n.d. Quote: "So in 2006, we started Python 3.0."

[178] https://www.python.org/doc/sunset-python-2/, "Sunsetting Python 2 (python.org)", n.d. Quote: "So, in 2008, we announced that we would sunset Python 2 in 2015, and asked people to upgrade before then."

[179] https://peps.python.org/pep-0373/, "PEP 373, Python 2.7 Release Schedule", n.d. Quote: "Support officially stopped January 1 2020, and 2.7.18 code freeze occurred on January 1 2020, but the final release occurred after that date."

[180] https://www.artima.com/articles/the-philosophy-of-ruby, "The Philosophy of Ruby, A Conversation with Yukihiro Matsumoto, Part I (Bill Venners)", 2003-09-29. Quote: "Instead of emphasizing the what, I want to emphasize the how part: how we feel while programming."

[181] https://www.artima.com/articles/the-philosophy-of-ruby, "The Philosophy of Ruby, A Conversation with Yukihiro Matsumoto, Part I (Bill Venners)", 2003-09-29. Quote: "I tried to make Ruby perfect for me, but maybe it's not perfect for you."

[182] https://www.ruby-lang.org/en/about/, "About Ruby (ruby-lang.org)", n.d. Quote: "I wanted a scripting language that was more powerful than Perl, and more object-oriented than Python"

[183] https://www.artima.com/articles/the-philosophy-of-ruby, "The Philosophy of Ruby, A Conversation with Yukihiro Matsumoto, Part I (Bill Venners)", 2003-09-29. Quote: "I believe consistency and orthogonality are tools of design, not the primary goal in design."

[184] https://www.ruby-lang.org/en/about/, "About Ruby (ruby-lang.org)", n.d. Quote: "Much of the growth is attributed to the popularity of software written in Ruby, particularly the Ruby on Rails web framework."

[185] https://rubyonrails.org/doctrine, "The Rails Doctrine (David Heinemeier Hansson)", n.d. Quote: "to place the happiness of the programmer on a pedestal"

[186] https://rubygems.org/pages/about, "About RubyGems.org", n.d. Quote: "Welcome to RubyGems.org, the Ruby community's gem hosting service."

[187] https://bundler.io/, "Bundler, The best way to manage a Ruby application's gems", n.d. Quote: "Bundler is an exit from dependency hell, and ensures that the gems you need are present in development, staging, and production."

[188] https://docs.ruby-lang.org/en/master/Ractor.html, "Ractor (Ruby documentation)", n.d. Quote: "On CRuby (the default implementation), the Global Virtual Machine Lock (GVL) is held per ractor, so ractors can run in parallel."

[189] https://www.ruby-lang.org/en/news/2020/12/25/ruby-3-0-0-released/, "Ruby 3.0.0 Released", 2020-12-25. Quote: "The specification and implementation are not matured and may be changed in the future, so this feature is marked as experimental"

[190] https://www.ruby-lang.org/en/news/2013/02/24/ruby-2-0-0-p0-is-released/, "Ruby 2.0.0-p0 is released", 2013-02-24. Quote: "It will be easier to migrate from 1.9 to 2.0 than it was from 1.8 to 1.9."

[191] https://www.ruby-lang.org/en/news/2014/07/01/eol-for-1-8-7-and-1-9-2/, "EOL for Ruby 1.8.7 and 1.9.2", 2014-07-01. Quote: "Last December, we extended the supported lifetime of Ruby versions 1.8.7 and 1.9.2 by six months."

[192] https://docs.ruby-lang.org/en/master/extension_rdoc.html, "Creating Extension Libraries for Ruby (Ruby documentation)", n.d. Quote: "In C, variables have types and data do not have types. In contrast, Ruby variables do not have a static type, and data themselves have types, so data will need to be converted between the languages."

[193] https://docs.ruby-lang.org/en/master/extension_rdoc.html, "Creating Extension Libraries for Ruby (Ruby documentation)", n.d. Quote: "Converting to the wrong data type may cause serious problems."

[194] https://raw.githubusercontent.com/ffi/ffi/master/README.md, "Ruby-FFI README", n.d. Quote: "Ruby-FFI is a gem for programmatically loading dynamically-linked native libraries, binding functions within them, and calling those functions from Ruby code."

[195] https://www.lua.org/doc/hopl.pdf, "The Evolution of Lua", 2007. Quote: "Lua is a scripting language born in 1993 at PUC-Rio, the Pontifical Catholic University of Rio de Janeiro in Brazil."

[196] https://www.lua.org/doc/hopl.pdf, "The Evolution of Lua", 2007. Quote: "From the start, Lua was designed to be simple, small, portable, fast, and easily embedded into applications."

[197] https://web.archive.org/web/20210202183858/https://queue.acm.org/detail.cfm?id=1983083, "Passing a Language through the Eye of a Needle (ACM Queue, archive.org copy)", 2011-05-12. Quote: "Lua itself is entirely implemented as a library, with the goal of being embedded in other applications."

[198] https://www.lua.org/doc/hopl.pdf, "The Evolution of Lua", 2007. Quote: "One thing that has worked really well was the early decision (made in Lua 1.0) to have tables as the sole datastructuring mechanism in Lua."

[199] https://www.lua.org/doc/hopl.pdf, "The Evolution of Lua", 2007. Quote: "Lua 4.0 brought two main novelties in the C API: support for multiple Lua states and a virtual stack for exchanging values between C and Lua."

[200] https://web.archive.org/web/20210202183858/https://queue.acm.org/detail.cfm?id=1983083, "Passing a Language through the Eye of a Needle (ACM Queue, archive.org copy)", 2011-05-12. Quote: "A key property of the Lua API is that it offers no way for C code to refer directly to Lua objects; any value to be manipulated by C code must be on the stack."

[201] https://www.lua.org/doc/hopl.pdf, "The Evolution of Lua", 2007. Quote: "We should have introduced booleans from the start, but we wanted to start with the simplest possible language."

[202] https://www.lua.org/doc/hopl.pdf, "The Evolution of Lua", 2007. Quote: "Automatic coercion of strings to numbers in arithmetic operations, which we took from Awk, could have been omitted."

[203] https://www.lua.org/doc/hopl.pdf, "The Evolution of Lua", 2007. Quote: "The lack of a common policy for building modules and installing packages prevents different groups from sharing code and discourages the development of a community code base."

[204] https://www.lua.org/doc/hopl.pdf, "The Evolution of Lua", 2007. Quote: "Tecgraf, Lua’s birthplace, never upgraded from Lua 3.2 to Lua 4.0 because of the big changes in the API."

[205] https://www.lua.org/wshop11/ws2011.pdf, "Some thorny points in the design of Lua: a personal perspective (Lua Workshop 2011 slides)", 2011-09. Quote: "Historical reason: Fortran used 1-based arrays, and most first users of Lua had a Fortran background."

[206] https://web.archive.org/web/20210202183858/https://queue.acm.org/detail.cfm?id=1983083, "Passing a Language through the Eye of a Needle (ACM Queue, archive.org copy)", 2011-05-12. Quote: "An FFI is not enough to allow a function in the system language to do all that a function in the script can do."

[207] https://www.stroustrup.com/1995_Java_whitepaper.pdf, "The Java Language Environment: A White Paper (Gosling and McGilton, Sun Microsystems; copy hosted by Stroustrup)", 1995-10. Quote: "Write your applications once, and you never need to port them"

[208] https://www.stroustrup.com/1995_Java_whitepaper.pdf, "The Java Language Environment: A White Paper (Gosling and McGilton, Sun Microsystems; copy hosted by Stroustrup)", 1995-10. Quote: "The interpreted nature of Java solves both the binary distribution problem and the version problem"

[209] https://www.stroustrup.com/1995_Java_whitepaper.pdf, "The Java Language Environment: A White Paper (Gosling and McGilton, Sun Microsystems; copy hosted by Stroustrup)", 1995-10. Quote: "Explicit memory management has proved to be a fruitful source of bugs, crashes, memory leaks, and poor performance."

[210] https://www.stroustrup.com/1995_Java_whitepaper.pdf, "The Java Language Environment: A White Paper (Gosling and McGilton, Sun Microsystems; copy hosted by Stroustrup)", 1995-10. Quote: "Automatic garbage collection is an integral part of Java and its run-time system."

[211] https://learn.microsoft.com/en-us/dotnet/standard/native-interop/pinvoke, "Platform Invoke (P/Invoke) (Microsoft Learn)", n.d. Quote: "P/Invoke is a technology that allows you to access structs, callbacks, and functions in unmanaged libraries from your managed code."

[212] https://kotlinlang.org/docs/java-interop.html, "Java interoperability - Kotlin Documentation", n.d. Quote: "Existing Java code can be called from Kotlin in a natural way, and Kotlin code can be used from Java rather smoothly as well."

[213] https://www.artima.com/articles/failure-and-exceptions, "Failure and Exceptions: A Conversation with James Gosling, Part II", 2003-09-22. Quote: "In Java you can ignore exceptions, but you have to willfully do it."

[214] https://www.artima.com/articles/the-trouble-with-checked-exceptions, "The Trouble with Checked Exceptions: A Conversation with Anders Hejlsberg, Part II", 2003-08-18. Quote: "It is a breaking change for me to add D to the throws clause of that method, because existing caller of that method will almost certainly not handle that exception."

[215] https://www.artima.com/articles/the-trouble-with-checked-exceptions, "The Trouble with Checked Exceptions: A Conversation with Anders Hejlsberg, Part II", 2003-08-18. Quote: "In the large, checked exceptions become such an irritation that people completely circumvent the feature."

[216] https://openjdk.org/projects/valhalla/design-notes/in-defense-of-erasure, "Background: How We Got the Generics We Have (Brian Goetz)", 2020-06. Quote: "It must be possible to evolve an existing non-generic class to be generic in a binary-compatible and source-compatible manner."

[217] https://openjdk.org/projects/valhalla/design-notes/in-defense-of-erasure, "Background: How We Got the Generics We Have (Brian Goetz)", 2020-06. Quote: "erasure was in fact the sensible and pragmatic choice for adding generics to Java in 2004"

[218] https://www.artima.com/articles/generics-in-c-java-and-c, "Generics in C#, Java, and C++: A Conversation with Anders Hejlsberg, Part VII", 2004-01-26. Quote: "When you apply reflection to a generic List in Java, you can't tell what the List is a List of."

[219] https://www.infoq.com/presentations/Null-References-The-Billion-Dollar-Mistake-Tony-Hoare/, "Null References: The Billion Dollar Mistake (Tony Hoare, InfoQ presentation abstract)", 2009-08-25. Quote: "Tony Hoare introduced Null references in ALGOL W back in 1965"

[220] https://openjdk.org/jeps/454, "JEP 454: Foreign Function & Memory API", n.d. (delivered in Java 22). Quote: "Replace the brittle machinery of native methods and the Java Native Interface (JNI) with a concise, readable, and pure-Java API."

[221] https://openjdk.org/jeps/454, "JEP 454: Foreign Function & Memory API", n.d. (delivered in Java 22). Quote: "JNI involves several tedious artifacts: a Java API (native methods), a C header file derived from the Java API, and a C implementation that calls the native library of interest."

[222] https://openjdk.org/jeps/454, "JEP 454: Foreign Function & Memory API", n.d. (delivered in Java 22). Quote: "JNI does not reconcile the Java type system with the C type system."

[223] https://openjdk.org/jeps/454, "JEP 454: Foreign Function & Memory API", n.d. (delivered in Java 22). Quote: "Over the years, numerous frameworks have emerged to fill the gaps left by JNI, including JNA, JNR and JavaCPP."

[224] https://openjdk.org/jeps/454, "JEP 454: Foreign Function & Memory API", 2024-10-07. Quote: "All memory segments provide spatial and temporal bounds which ensure that memory access operations are safe."

[225] https://wirfs-brock.com/allen/jshopl.pdf, "JavaScript: The First 20 Years (Wirfs-Brock and Eich, HOPL IV)", 2020-06. Quote: "How a sidekick scripting language for Java, created at Netscape in a ten-day hack, ships first as a de facto Web standard"

[226] https://wirfs-brock.com/allen/jshopl.pdf, "JavaScript: The First 20 Years (Wirfs-Brock and Eich, HOPL IV)", 2020-06. Quote: "it was clear Java was not simple enough for the “glue” scripters."

[227] https://wirfs-brock.com/allen/jshopl.pdf, "JavaScript: The First 20 Years (Wirfs-Brock and Eich, HOPL IV)", 2020-06. Quote: "Node.js enabled Web programmers who had mastered JavaScript to transfer their skills to other kinds of applications and non-browser environments."

[228] https://wirfs-brock.com/allen/jshopl.pdf, "JavaScript: The First 20 Years (Wirfs-Brock and Eich, HOPL IV)", 2020-06. Quote: "Automatic coercions were intended to lower the entry barrier for the initial adoption of JavaScript as a simple scripting language."

[229] https://wirfs-brock.com/allen/jshopl.pdf, "JavaScript: The First 20 Years (Wirfs-Brock and Eich, HOPL IV)", 2020-06. Quote: "as JavaScript evolved into a general purpose language the coercions have proven to be a significant source of confusion and coding bugs."

[230] https://wirfs-brock.com/allen/jshopl.pdf, "JavaScript: The First 20 Years (Wirfs-Brock and Eich, HOPL IV)", 2020-06. Quote: "Shon Katzenberger successfully argued that it was too late to make such a change given the large number of existing Web pages it would break."

[231] https://wirfs-brock.com/allen/jshopl.pdf, "JavaScript: The First 20 Years (Wirfs-Brock and Eich, HOPL IV)", 2020-06. Quote: "The first constraint is sometimes expressed using the slogan “Don’t break the Web!”"

[232] https://wirfs-brock.com/allen/jshopl.pdf, "JavaScript: The First 20 Years (Wirfs-Brock and Eich, HOPL IV)", 2020-06. Quote: "while Crockford and many others disliked JavaScript’s Automatic Semicolon Insertions, many developers preferred to code without explicit semicolons."

[233] https://nodejs.org/en/learn/modules/abi-stability, "ABI Stability (Node.js Learn)", n.d. Quote: "the application has to be recompiled, reinstalled, and redeployed whenever a new major version of Node.js is introduced into the production environment."

[234] https://nodejs.org/api/n-api.html, "Node-API (Node.js documentation)", n.d. Quote: "It is intended to insulate addons from changes in the underlying JavaScript engine and allow modules compiled for one major version to run on later major versions of Node.js without recompilation."

[235] https://nodejs.org/api/ffi.md, "FFI (Node.js API documentation)", n.d. (added in v26.1.0). Quote: "The `node:ffi` module provides an experimental foreign function interface for loading dynamic libraries and calling native symbols from JavaScript."

[236] https://www.computerwoche.de/a/php-creator-extolls-language-s-modest-start,2518219, "PHP creator extolls language's modest start (Computerwoche, IDG news report)", 2012-07-19. Quote: "I had no intention of writing a language. I didn't have a clue how to write a language."

[237] https://www.computerwoche.de/a/php-creator-extolls-language-s-modest-start,2518219, "PHP creator extolls language's modest start (Computerwoche, IDG news report)", 2012-07-19. Quote: "The focus wasn't on the language itself, but on the ecosystem"

[238] https://www.computerwoche.de/a/php-creator-extolls-language-s-modest-start,2518219, "PHP creator extolls language's modest start (Computerwoche, IDG news report)", 2012-07-19. Quote: "ISPs (Internet Service Providers), in particular, found the technology useful because it worked very well in shared environments."

[239] https://www.computerwoche.de/a/php-creator-extolls-language-s-modest-start,2518219, "PHP creator extolls language's modest start (Computerwoche, IDG news report)", 2012-07-19. Quote: "You can have the prettiest language in the world, but if it can't talk to the back end, and you have no way to host it, it is useless."

[240] https://blog.simonwillison.net/2003/Jun/24/philosophy/, "PHP philosophy, and bundling SQLite (Simon Willison, quoting Rasmus Lerdorf)", 2003-06-24. Quote: "PHP is not about purity in CS principles or architecture, it is about solving the ugly web problem with an admittedly ugly, but extremely functional and convenient solution."

[241] https://www.php.net/manual/en/history.php.php, "History of PHP (PHP manual)", n.d. Quote: "The syntax itself was similar to that of Perl, albeit much more limited, simple, and somewhat inconsistent."

[242] https://wiki.php.net/rfc/php6, "PHP RFC: Name of Next Release of PHP", n.d. Quote: "there has been a previous attempt at a new major version, which was started in 2005 and abandoned in 2010 due to difficulties in the Unicode implementation."

[243] https://www.php.net/releases/7_0_0.php, "PHP 7.0.0 Release Announcement", n.d. Quote: "Improved performance: PHP 7 is up to twice as fast as PHP 5.6"

[244] https://wiki.php.net/rfc/ffi, "PHP RFC: FFI - Foreign Function Interface", 2018-12-04. Quote: "Status: Implemented (in PHP 7.4) First Published at"

[245] https://wiki.php.net/rfc/ffi, "PHP RFC: FFI - Foreign Function Interface", 2018-12-04. Quote: "Proposal It is proposed to extend PHP with a simple FFI API designed after LuaJTI/FFI and Python/CFFI (the latter was actually based on former)."

[246] https://wiki.php.net/rfc/ffi, "PHP RFC: FFI - Foreign Function Interface", 2018-12-04. Quote: "in pure PHP, without having to have deep knowledge in the Zend extension API, and without having to learn a 3rd “intermediate” language."

[247] https://www.artima.com/articles/versioning-virtual-and-override, "Versioning, Virtual, and Override: A Conversation with Anders Hejlsberg, Part IV", 2003-09-15. Quote: "versioning ended up being one of the pillars of our language design."

[248] https://www.artima.com/articles/the-c-design-process, "The C# Design Process: A Conversation with Anders Hejlsberg, Part I", 2003-08-04. Quote: "good language design boils down to assembling a team of people who have good taste."

[249] https://www.artima.com/articles/generics-in-c-java-and-c, "Generics in C#, Java, and C++: A Conversation with Anders Hejlsberg, Part VII", 2004-01-26. Quote: "Typically with value types, you really do care that List<int> is int."

[250] https://learn.microsoft.com/en-us/dotnet/csharp/language-reference/builtin-types/struct, "Structure types (Microsoft Learn)", n.d. Quote: "A structure type (or struct type) is a value type that can encapsulate data and related functionality."

[251] https://learn.microsoft.com/en-us/dotnet/standard/memory-and-spans/, "Memory and spans (Microsoft Learn)", n.d. Quote: "These types are designed to allow the creation of algorithms that avoid copying memory or allocating on the managed heap more than necessary."

[252] https://devblogs.microsoft.com/dotnet/nullable-reference-types-in-csharp/, "Introducing Nullable Reference Types in C# (.NET Blog, Mads Torgersen)", 2017-11-15. Quote: "Languages like C# don't let you express whether a null right here is a good idea or not."

[253] https://devblogs.microsoft.com/dotnet/nullable-reference-types-in-csharp/, "Introducing Nullable Reference Types in C# (.NET Blog, Mads Torgersen)", 2017-11-15. Quote: "Today we released a prototype of a C# feature called “nullable reference types“, which is intended to help you find and fix most of your null-related bugs before they blow up at runtime."

[254] https://learn.microsoft.com/en-us/dotnet/csharp/whats-new/csharp-version-history, "The history of C# (Microsoft Learn)", n.d. Quote: "Nullable reference types, while implemented in the compiler, is much more useful when libraries are annotated to provide semantic information regarding the null state of arguments and return values."

[255] https://learn.microsoft.com/en-us/dotnet/csharp/nullable-references, "Nullable reference types (Microsoft Learn)", n.d. Quote: "Nullable reference types are entirely a compile-time feature."

[256] https://learn.microsoft.com/en-us/dotnet/csharp/language-reference/unsafe-code, "Unsafe code, pointer types, and function pointers (Microsoft Learn)", n.d. Quote: "Unsafe code isn't necessarily dangerous; it's just code whose safety can't be verified by .NET tools."

[257] https://go.dev/talks/2012/splash.article, "Go at Google: Language Design in the Service of Software Engineering", 2012-10-25. Quote: "Go is a programming language designed by Google to help solve Google's problems, and Google has big problems."

[258] https://go.dev/talks/2012/splash.article, "Go at Google: Language Design in the Service of Software Engineering", 2012-10-25. Quote: "a language that is not a breakthrough research language but is nonetheless an excellent tool for engineering large software projects"

[259] https://go.dev/talks/2012/splash.article, "Go at Google: Language Design in the Service of Software Engineering", 2012-10-25. Quote: "a factor of fifty less to do turns minutes into seconds, coffee breaks into interactive builds"

[260] https://web.archive.org/web/2023/https://cacm.acm.org/magazines/2022/5/260357-the-go-programming-language-and-environment/fulltext, "The Go Programming Language and Environment (Communications of the ACM, May 2022; Internet Archive copy)", 2022-05. Quote: "When building a particular program, Go resolves competing required versions of a dependency module by selecting the maximum"

[261] https://go.dev/doc/go1compat, "Go 1 and the Future of Go Programs", n.d. Quote: "It is intended that programs written to the Go 1 specification will continue to compile and run correctly, unchanged, over the lifetime of that specification."

[262] https://web.archive.org/web/2023/https://cacm.acm.org/magazines/2022/5/260357-the-go-programming-language-and-environment/fulltext, "The Go Programming Language and Environment (Communications of the ACM, May 2022; Internet Archive copy, because the CACM site blocked direct fetch)", 2022-05. Quote: "native support for concurrency and parallelism helped take advantage of the multicore machines that were becoming mainstream at the time"

[263] https://web.archive.org/web/2023/https://cacm.acm.org/magazines/2022/5/260357-the-go-programming-language-and-environment/fulltext, "The Go Programming Language and Environment (Communications of the ACM, May 2022; Internet Archive copy)", 2022-05. Quote: "We considered a handful of designs during Go's first decade but only recently found one that we feel fits Go well."

[264] https://go.dev/doc/faq, "Frequently Asked Questions (FAQ) - The Go Programming Language", n.d. Quote: "Generics are convenient but they come at a cost in complexity in the type system and run-time."

[265] https://research.swtch.com/generic, "The Generic Dilemma", 2009-12. Quote: "The generic dilemma is this: do you want slow programmers, slow compilers and bloated binaries, or slow execution times?"

[266] https://go.dev/doc/go1.18, "Go 1.18 Release Notes", 2022-03. Quote: "These new language changes required a large amount of new code that has not had significant testing in production settings."

[267] https://go.dev/doc/faq, "Frequently Asked Questions (FAQ) - The Go Programming Language", n.d. Quote: "This situation can be confusing, and arises when a nil value is stored inside an interface value such as an error return"

[268] https://go.dev/blog/error-syntax, "[ On | No ] syntactic support for error handling", n.d. Quote: "For the foreseeable future, the Go team will stop pursuing syntactic language changes for error handling."

[269] https://pkg.go.dev/cmd/cgo, "cgo command - cmd/cgo - Go Packages", n.d. Quote: "Cgo enables the creation of Go packages that call C code."

[270] https://dave.cheney.net/2016/01/18/cgo-is-not-go, "cgo is not Go", 2016-01-18. Quote: "By default cgo is disabled when cross compiling."

[271] https://dave.cheney.net/2016/01/18/cgo-is-not-go, "cgo is not Go", 2016-01-18. Quote: "the source of your program contains enough information for a tool to build the project"

[272] https://doc.rust-lang.org/book/ch04-01-what-is-ownership.html, "What Is Ownership? - The Rust Programming Language", n.d. Quote: "Ownership is a set of rules that govern how a Rust program manages memory."

[273] https://security.googleblog.com/2024/09/eliminating-memory-safety-vulnerabilities-Android.html, "Eliminating Memory Safety Vulnerabilities at the Source", 2024-09-25. Quote: "the percentage of memory safety vulnerabilities in Android dropped from 76% to 24% over 6 years as development shifted to memory safe languages"

[274] https://security.googleblog.com/2024/09/eliminating-memory-safety-vulnerabilities-Android.html, "Eliminating Memory Safety Vulnerabilities at the Source", 2024-09-25. Quote: "The Android team has observed that the rollback rate of Rust changes is less than half that of C++."

[275] https://doc.rust-lang.org/edition-guide/editions/index.html, "What are editions? - The Rust Edition Guide", n.d. Quote: "crates in one edition must seamlessly interoperate with those compiled with other editions"

[276] https://raw.githubusercontent.com/rust-lang/rfcs/master/text/2052-epochs.md, "RFC 2052: Epochs (later renamed editions)", 2017-06-26. Quote: "If we don't, we risk splitting the ecosystem, which is extremely dangerous."

[277] https://doc.rust-lang.org/book/ch01-03-hello-cargo.html, "Hello, Cargo! - The Rust Programming Language", n.d. Quote: "Most Rustaceans use this tool to manage their Rust projects because Cargo handles a lot of tasks for you"

[278] https://web.archive.org/web/20231229212320/https://graydon2.dreamwidth.org/307291.html, "The Rust I Wanted Had No Future (Internet Archive copy; the live URL did not return the post to curl)", 2023-06-04. Quote: "the Rust We Got is many, many miles away from The Rust I Wanted"

[279] https://web.archive.org/web/20231229212320/https://graydon2.dreamwidth.org/307291.html, "The Rust I Wanted Had No Future (Internet Archive copy)", 2023-06-04. Quote: "I would have traded performance and expressivity away for simplicity"

[280] https://web.archive.org/web/20231229212320/https://graydon2.dreamwidth.org/307291.html, "The Rust I Wanted Had No Future (Internet Archive copy)", 2023-06-04. Quote: "failure to do so is also a big part of Rust's terrible compile times and lack of a stable ABI"

[281] https://blog.rust-lang.org/2025/02/13/2024-State-Of-Rust-Survey-results/, "2024 State of Rust Survey Results", 2025-02-13. Quote: "Perhaps unsurprisingly, slow compilation was at the top of the list, as it seems to be a perennial concern of Rust users."

[282] https://web.archive.org/web/20231229212320/https://graydon2.dreamwidth.org/307291.html, "The Rust I Wanted Had No Future (Internet Archive copy)", 2023-06-04. Quote: "The error-handling system that shipped in 1.0 was basically a void resulting from ripping out a couple previous attempts"

[283] https://doc.rust-lang.org/book/ch20-01-unsafe-rust.html, "Unsafe Rust - The Rust Programming Language", n.d. Quote: "Unsafe Rust exists because, by nature, static analysis is conservative."

[284] https://doc.rust-lang.org/nomicon/ffi.html, "Foreign Function Interface - The Rustonomicon", n.d. Quote: "Rust guarantees that the layout of a struct is compatible with the platform"

[285] https://doc.rust-lang.org/stable/reference/items/external-blocks.html, "External blocks - The Rust Reference", n.d. Quote: "This is the default when a function is declared without using extern fn. The Rust ABI offers no stability guarantees."

[286] https://github.com/rust-lang/compiler-team/issues/631, "Experimental feature gate for extern "crabi" ABI (major change proposal #631)", 2023-05-09. Quote: "often have to use the C ABI as a lowest-common-denominator for cross-language function calls"

[287] https://github.com/rust-lang/compiler-team/issues/631, "Experimental feature gate for extern "crabi" ABI (major change proposal #631)", 2023-05-09. Quote: "There will need to be a full RFC before this can be stabilized"

[288] https://security.googleblog.com/2024/09/eliminating-memory-safety-vulnerabilities-Android.html, "Eliminating Memory Safety Vulnerabilities at the Source", 2024-09-25. Quote: "Android is focusing on making interoperability safe and convenient as a primary capability in our memory safety journey."

[289] https://docs.kernel.org/rust/general-information.html, "General Information - The Linux Kernel documentation (Rust)", n.d. Quote: "Bindings are the declarations for Rust of those functions and types from the C side."

[290] https://nondot.org/sabre/, "Chris Lattner's Homepage", n.d. Quote: "I started work on the Swift Programming Language ( wikipedia ) in July of 2010."

[291] https://www.hackingwithswift.com/interviews/chris-lattner-could-apple-have-improved-objective-c-instead-of-making-swift, "Could Apple have improved Objective-C instead of making Swift? (interview with Chris Lattner)", 2020-06-18. Quote: "ARC came to Objective-C as part of Swift driving it, because we needed safety and memory management"

[292] https://raw.githubusercontent.com/swiftlang/swift-book/main/TSPL.docc/LanguageGuide/TheBasics.md, "The Basics - The Swift Programming Language (source of the Swift book)", n.d. Quote: "Optionals ensure that code always checks whether a value is missing before using the value, and non-optional values are guaranteed to never be missing."

[293] https://raw.githubusercontent.com/swiftlang/swift/main/docs/CppInteroperability/CppInteroperabilityManifesto.md, "C++ Interoperability Manifesto (Swift repository)", n.d. Quote: "Swift/C++ interoperability builds on top of the Swift/C interoperability, so it helps to be familiar with"

[294] https://www.hackingwithswift.com/interviews/chris-lattner-could-apple-have-improved-objective-c-instead-of-making-swift, "Could Apple have improved Objective-C instead of making Swift? (interview with Chris Lattner, recorded 2020-06-18)", 2020-06-18. Quote: "Swift is risky . This could fragment the community."

[295] https://raw.githubusercontent.com/swiftlang/swift/main/docs/ABIStabilityManifesto.md, "Swift ABI Stability Manifesto", n.d. Quote: "Without source compatibility, projects face version-lock where all source code in a project and its packages must be written in the same version of Swift."

[296] https://www.swift.org/blog/abi-stability-and-more/, "ABI Stability and More", 2019-02-07. Quote: "Swift 5 provides binary compatibility for apps: a guarantee that going forward, an app built with one version of the Swift compiler will be able to talk to a library built with another version."

[297] https://www.swift.org/blog/abi-stability-and-more/, "ABI Stability and More", 2019-02-07. Quote: "ABI stability is implemented for each operating system that it compiles and runs on."

[298] https://www.swift.org/blog/abi-stability-and-more/, "ABI Stability and More", 2019-02-07. Quote: "That is, the app developer and the library author have to be using the same version of the compiler."

[299] https://www.swift.org/blog/library-evolution/, "Library Evolution in Swift", 2020-02-13. Quote: "enabling library evolution support for a framework is itself a binary-incompatible change"

[300] https://www.swift.org/blog/library-evolution/, "Library Evolution in Swift", 2020-02-13. Quote: "By restricting what kind of changes can be resilient in the future, some flexibility can be traded off for additional performance."

[301] https://ziglang.org/learn/why_zig_rust_d_cpp/, "Why Zig When There is Already C++, D, and Rust?", n.d. Quote: "No hidden control flow If Zig code doesn’t look like it’s jumping away to call a function, then it isn’t."

[302] https://ziglang.org/learn/why_zig_rust_d_cpp/, "Why Zig When There is Already C++, D, and Rust?", n.d. Quote: "The entire concept of the heap is managed by library and application code, not by the language."

[303] https://ziglang.org/learn/overview/, "Overview - Zig Programming Language", n.d. Quote: "As a result, the Zig Standard Library can be used even for the freestanding target."

[304] https://ziglang.org/, "Zig Programming Language (home page)", n.d. Quote: "A fresh approach to metaprogramming based on compile-time code execution and lazy evaluation."

[305] https://andrewkelley.me/post/zig-cc-powerful-drop-in-replacement-gcc-clang.html, "zig cc: a Powerful Drop-In Replacement for GCC/Clang", n.d. Quote: "What's new is that the zig cc sub-command is available, and it supports the same options as Clang, which, in turn, supports the same options as GCC."

[306] https://ziglang.org/learn/why_zig_rust_d_cpp/, "Why Zig When There is Already C++, D, and Rust?", n.d. Quote: "Zig is attempting to become the new portable language for libraries by simultaneously making it straightforward to conform to the C ABI for external functions"

[307] https://ziglang.org/documentation/master/, "Documentation - The Zig Programming Language (master)", n.d. Quote: "This documentation section will be updated before 1.0.0 is released, with a conclusive statement about the status of"

[308] https://arxiv.org/pdf/1411.1607, "Julia: A fresh approach to numerical computing (Bezanson, Edelman, Karpinski, Shah)", 2015-07-07. Quote: "The Two Language Problem: As long as the developers’ language is harder than the users’ language, numerical computing will always be hindered."

[309] https://julialang.org/blog/2012/02/why-we-created-julia/, "Why We Created Julia", 2012-02-14. Quote: "We want the speed of C with the dynamism of Ruby."

[310] https://arxiv.org/pdf/1411.1607, "Julia: A fresh approach to numerical computing", 2015-07-07. Quote: "Multiple dispatch, a technique from computer science, picks the right algorithm for the right circumstance."

[311] https://docs.julialang.org/en/v1/manual/calling-c-and-fortran-code/, "Calling C and Fortran Code - The Julia Language", n.d. Quote: "To allow easy use of this existing code, Julia makes it simple and efficient to call C and Fortran functions."

[312] https://arxiv.org/pdf/1411.1607, "Julia: A fresh approach to numerical computing", 2015-07-07. Quote: "one can take a pointer to the first array element and pass it to a C library function using ccall and it will just work"

[313] https://julialang.org/blog/2023/04/julia-1.9-highlights/, "Julia 1.9 Highlights", 2023-04. Quote: "the need to regenerate native code in each session meant that many packages still suffered from long TTFX latencies"

[314] https://julialang.org/blog/2023/04/julia-1.9-highlights/, "Julia 1.9 Highlights", 2023-04. Quote: "Cache files have also become larger due to the storage of more data and the use of a different serialization format."

[315] https://arxiv.org/pdf/1411.1607, "Julia: A fresh approach to numerical computing", 2015-07-07. Quote: "Indexing of arrays is performed with brackets, and is 1-based."

[316] https://blog.jetbrains.com/kotlin/2011/08/why-jetbrains-needs-kotlin/, "Why JetBrains needs Kotlin (Dmitry Jemerov)", n.d. (URL path dated 2011/08). Quote: "At the same time, we cannot accept compromises in terms of either Java interoperability (the new language is going to be introduced gradually, and needs to interoperate smoothly with the existing code base) or compilation speed (our code base takes long enough to compile with javac, and we cannot afford making it any slower)."

[317] https://kotlinlang.org/docs/null-safety.html, "Null safety - Kotlin Documentation", n.d. Quote: "Null safety is a Kotlin feature designed to significantly reduce the risk of null references, also known as The Billion-Dollar Mistake."

[318] https://android-developers.googleblog.com/2019/05/google-io-2019-empowering-developers-to-build-experiences-on-Android-Play.html, "Google I/O 2019: Empowering developers to build experiences on Android + Play", 2019-05-07. Quote: "Many new Jetpack APIs and features will be offered first in Kotlin."

[319] https://kotlinlang.org/docs/native-c-interop.html, "Interoperability with C - Kotlin Documentation", n.d. Quote: "Kotlin/Native comes with a cinterop tool, which you can use to quickly generate everything you need to interact with an external C library."

[320] https://kotlinlang.org/docs/native-c-interop.html, "Interoperability with C - Kotlin Documentation", n.d. Quote: "The C libraries import is in Beta. All Kotlin declarations generated by the cinterop tool from C libraries should have the @ExperimentalForeignApi annotation."

[321] https://graphics.stanford.edu/papers/brookgpu/brookgpu.pdf, "Brook for GPUs: Stream Computing on Graphics Hardware", 2004. Quote: "the user is forced to express their algorithm in terms of graphics primitives, such as textures and triangles"

[322] https://graphics.stanford.edu/papers/brookgpu/brookgpu.pdf, "Brook for GPUs: Stream Computing on Graphics Hardware", 2004. Quote: "We have created Brook implementations for both NVIDIA and ATI hardware, using both DirectX and OpenGL, as well as a CPU reference implementation."

[323] https://graphics.stanford.edu/papers/brookgpu/brookgpu.pdf, "Brook for GPUs: Stream Computing on Graphics Hardware", 2004. Quote: "The compiler maps Brook kernels into Cg shaders which are translated by vendor-provided shader compilers into GPU assembly."

[324] https://developer.nvidia.com/blog/inside-the-programming-evolution-of-gpu-computing, "Inside the Programming Evolution of GPU Computing", 2015-10-28. Quote: "The idea behind Brook, and of course, later, CUDA, was to create a programming approach that would resonate with any C programmer but offer the higher level parallel programming concepts that could be compiled to the GPU."

[325] https://docs.nvidia.com/cuda/cuda-programming-guide/01-introduction/introduction.html, "1.1. Introduction, CUDA Programming Guide", n.d. Quote: "In 2006, NVIDIA introduced the Compute Unified Device Architecture (CUDA) to enable any computational workload to use the throughput capability of GPUs independent of graphics APIs."

[326] https://docs.nvidia.com/cuda/cuda-programming-guide/01-introduction/programming-model.html, "1.2. Programming Model, CUDA Programming Guide", n.d. Quote: "When an application launches a kernel, it does so with many threads, often millions of threads."

[327] https://docs.nvidia.com/cuda/cuda-programming-guide/01-introduction/programming-model.html, "1.2. Programming Model, CUDA Programming Guide", n.d. Quote: "Within a thread block, threads are organized into groups of 32 threads called warps ."

[328] https://docs.nvidia.com/cuda/parallel-thread-execution/index.html, "1. Introduction, PTX ISA 9.4 documentation", n.d. Quote: "Provide a stable ISA that spans multiple GPU generations."

[329] https://docs.nvidia.com/cuda/cuda-programming-guide/01-introduction/cuda-platform.html, "1.3. The CUDA platform, CUDA Programming Guide", n.d. Quote: "This enables forward compatibility with future GPUs without the need to rebuild applications or libraries."

[330] https://docs.nvidia.com/cuda/cuda-programming-guide/01-introduction/cuda-platform.html, "1.3. The CUDA platform, CUDA Programming Guide", n.d. Quote: "Domain-specific languages and compilers for high-level languages can generate PTX code as an intermediate representation (IR)"

[331] https://www.modular.com/blog/democratizing-compute-part-2-what-exactly-is-cuda, "Modular: What exactly is CUDA? (Democratizing AI Compute, Part 2)", 2025-02-05. Quote: "start building rich and complicated closed-source, high-level libraries that abstract away low-level CUDA details"

[332] https://www.modular.com/blog/democratizing-compute-part-2-what-exactly-is-cuda, "Modular: What exactly is CUDA? (Democratizing AI Compute, Part 2)", 2025-02-05. Quote: "The cuDNN library is especially important in this story"

[333] https://www.modular.com/blog/democratizing-ai-compute-part-3-how-did-cuda-succeed, "Modular: How did CUDA succeed? (Democratizing AI Compute, Part 3)", 2025-02-12. Quote: "a key early NVIDIA strategy was to keep their GPUs compatible across generations"

[334] https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/understanding-memory.html, "2.6. Unified and System Memory, CUDA Programming Guide", n.d. Quote: "but this can be verbose and complicate software design"

[335] https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/intro-to-cuda-cpp.html, "2.1. Intro to CUDA C++, CUDA Programming Guide", n.d. Quote: "Unified memory is a feature of the CUDA runtime which lets the NVIDIA Driver manage movement of data between host and device(s)."

[336] https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/writing-tile-kernels.html, "2.4. Writing Tile Kernels, CUDA Programming Guide", n.d. Quote: "Tile programming allows programmers to express parallelism differently and leaves the lowest level of parallelism to the compiler and built-in operations."

[337] https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/writing-tile-kernels.html, "2.4. Writing Tile Kernels, CUDA Programming Guide", n.d. Quote: "The compiler handles all thread-level indexing within the block."

[338] https://docs.nvidia.com/cuda/parallel-thread-execution/index.html, "1. Introduction, PTX ISA 9.4 documentation", n.d. Quote: "PTX programs are translated at install time to the target hardware instruction set."

[339] https://cvw.cac.cornell.edu/gpu-migration-portability/gpu-portability-solutions/gpu-portability-interfaces, "Cornell Virtual Workshop, GPU Portability Solutions, Vendor-Supported Interfaces", n.d. Quote: "Source code that is written to run on one GPU platform will generally not run on a different one, unless significant changes are made to the code."

[340] https://docs.nvidia.com/cuda/eula/index.html, "1. License Agreement for NVIDIA Software Development Kits, EULA", 2026-01-26. Quote: "You may not reverse engineer, decompile or disassemble any portion of the output generated using SDK elements for the purpose of translating such output artifacts to target a non-NVIDIA platform."

[341] https://www.modular.com/blog/democratizing-ai-compute-part-3-how-did-cuda-succeed, "Modular: How did CUDA succeed? (Democratizing AI Compute, Part 3)", 2025-02-12. Quote: "CUDA is a developer platform built through brilliant execution, deep strategic investment, continuity, ecosystem lock-in"

[342] https://www.eecs.harvard.edu/~htk/publication/2019-mapl-tillet-kung-cox.pdf, "Triton: An Intermediate Language and Compiler for Tiled Neural Network Computations", 2019-06-22. Quote: "operations that cannot leverage existing vendor libraries (e.g., cuBLAS, cuDNN) are at risk of facing poor device utilization unless custom implementations are written by experts"

[343] https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/nvcc.html, "2.7. NVCC: The NVIDIA CUDA Compiler, CUDA Programming Guide", n.d. Quote: "nvcc separates the device code from the host code and dispatches their compilation to the GPU and the host compilers, respectively."

[344] https://www.khronos.org/opencl/, "OpenCL - The Open Standard for Parallel Programming of Heterogeneous Systems", n.d. Quote: "An OpenCL application is split into host and device parts with host code written using a general programming language such as C or C++ and compiled by a conventional compiler for execution on a host CPU."

[345] https://www.khronos.org/opencl/, "OpenCL - The Open Standard for Parallel Programming of Heterogeneous Systems", n.d. Quote: "The device compilation phase can be done online, i.e. during execution of an application using special API calls."

[346] https://www.khronos.org/news/press/khronos-group-releases-opencl-3.0, "Khronos Group Releases OpenCL 3.0", 2020-04-27. Quote: "To cater to a widening diversity of OpenCL devices, OpenCL 3.0 makes all functionality beyond version 1.2 optional."

[347] https://www.khronos.org/news/press/khronos-group-releases-opencl-3.0, "Khronos Group Releases OpenCL 3.0", 2020-04-27. Quote: "OpenCL 2.X delivers significant functionality, but OpenCL 1.2 has proven itself as the baseline needed by all vendors and markets."

[348] https://forums.developer.nvidia.com/t/opencl-2-x-support-plans/44215, "OpenCL 2.x support plans? - CUDA Programming and Performance - NVIDIA Developer Forums", 2016-08. Quote: "There was a considerable period of time where OpenCL 1.2 (and later 2.x) were published, and NVIDIA drivers only supported OpenCL 1.1."

[349] https://developer.apple.com/tutorials/data/documentation/macos-release-notes/macos-mojave-10_14-release-notes.json, "macOS Mojave 10.14 Release Notes", n.d. Quote: "The APIs in the OpenGL and OpenCL frameworks are deprecated and remain present for compatibility purposes."

[350] https://www.intel.com/content/www/us/en/docs/oneapi/programming-guide/2024-0/migrating-from-opencl-code-to-sycl.html, "Migrating from OpenCL Code to SYCL", n.d. Quote: "somewhat verbose setup code that goes with offloading kernels on devices"

[351] https://lists.w3.org/Archives/Public/public-gpu/2018Nov/0003.html, "Re: Binary vs Text from Ken Russell on 2018-11-06", 2018-11-06. Quote: "dealing with hundreds of bugs associated with parsing, validating, and passing a textual shading language through to underlying drivers"

[352] https://lists.w3.org/Archives/Public/public-gpu/2018Nov/0003.html, "Re: Binary vs Text from Ken Russell on 2018-11-06", 2018-11-06. Quote: "Of more concern were situations where we had to semi-arbitrarily restrict the source language"

[353] https://api.github.com/repos/gpuweb/gpuweb/issues/44, "Position: Directly ingest SPIR-V, and not a human-writable shading language", 2018-01-02. Quote: "these types of bugs are inherent to ingesting a text-based language"

[354] https://lists.w3.org/Archives/Public/public-gpu/2018Nov/0002.html, "Re: Binary vs Text from Florian Bosch on 2018-11-06", 2018-11-06. Quote: "Since every GPU vendor needs to implement a full parser/compiler toolchain, it bloats the driver and introduces interpretation ambiguity where some constructs are not reliably translated to GPU code"

[355] https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html, "SPIR-V Specification (unified, version 1.6)", n.d. Quote: "Be the form passed by a client API into a driver to set shaders/kernels."

[356] https://registry.khronos.org/SPIR-V/papers/WhitePaper.html, "A Khronos-Defined Intermediate Language for Native Representation of Graphical Shaders and Compute Kernels (SPIR-V white paper)", n.d. Quote: "There is no need for every hardware platform to provide its own high-level language translator."

[357] https://registry.khronos.org/SPIR-V/papers/WhitePaper.html, "A Khronos-Defined Intermediate Language for Native Representation of Graphical Shaders and Compute Kernels (SPIR-V white paper)", n.d. Quote: "Getting independent implementations to match SPIR-V processing is correspondingly simpler."

[358] https://www.khronos.org/news/press/khronos-releases-vulkan-1-0-specification, "Khronos Releases Vulkan 1.0 Specification", 2016-02-16. Quote: "Eliminating the need for a built-in high-level language source compiler significantly reduces GPU driver complexity and will enable a diversity of language front-ends."

[359] https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html, "SPIR-V Specification (unified, version 1.6)", n.d. Quote: "Target-specific register allocation and scheduling are still expected to take significant time."

[360] https://registry.khronos.org/OpenGL/specs/gl/GLSLangSpec.4.60.html, "The OpenGL Shading Language, Version 4.60.8", n.d. Quote: "Independent offline tool chains will compile GLSL down to the SPIR-V intermediate language."

[361] https://raw.githubusercontent.com/microsoft/DirectXShaderCompiler/main/docs/DXIL.rst, "DirectX Intermediate Language (DXIL) design document, DirectXShaderCompiler", n.d. Quote: "DXIL is intended to support a direct mapping of the HLSL programming language into Low-Level Virtual Machine Intermediate Representation (LLVM IR), suitable for consumption in GPU drivers."

[362] https://raw.githubusercontent.com/microsoft/DirectXShaderCompiler/main/docs/DXIL.rst, "DirectX Intermediate Language (DXIL) design document, DirectXShaderCompiler", n.d. Quote: "DXIL is a contract between IR producers, such as compilers for HLSL and other domain-specific languages, and IR consumers, such as IHV driver JIT compilers or offline XBOX shader compiler."

[363] https://raw.githubusercontent.com/microsoft/DirectXShaderCompiler/main/docs/DXIL.rst, "DirectX Intermediate Language (DXIL) design document, DirectXShaderCompiler", n.d. Quote: "Microsoft will publicly document DXIL to attract domain language implementers and spur innovation."

[364] https://www.w3.org/TR/WGSL/, "WebGPU Shading Language (W3C)", n.d. Quote: "WGSL is statically typed: each value computed by a particular expression is in a specific type, determined only by examining the program source."

[365] https://www.w3.org/TR/WGSL/, "WebGPU Shading Language", 2026-10-21. Quote: "This clamping requirement is one place where WGSL mandates a meaningful result, but which would yield undefined behavior in C and C++"

[366] https://www.w3.org/TR/WGSL/, "WebGPU Shading Language", 2026-10-21. Quote: "This is a portability hazard, as different implementations may exhibit the different behaviors."

[367] https://www.eecs.harvard.edu/~htk/publication/2019-mapl-tillet-kung-cox.pdf, "Triton: An Intermediate Language and Compiler for Tiled Neural Network Computations", 2019-06-22. Quote: "We present Triton, a language and compiler centered around the concept of tile, i.e., statically shaped multi-dimensional sub-arrays."

[368] https://www.eecs.harvard.edu/~htk/publication/2019-mapl-tillet-kung-cox.pdf, "Triton: An Intermediate Language and Compiler for Tiled Neural Network Computations", 2019-06-22. Quote: "they are often much slower than vendor libraries in practice (see, e.g., Figure 1), and lack the expressivity necessary to implement structured sparsity patterns"

[369] https://www.eecs.harvard.edu/~htk/publication/2019-mapl-tillet-kung-cox.pdf, "Triton: An Intermediate Language and Compiler for Tiled Neural Network Computations", 2019-06-22. Quote: "This approach leads to simpler kernels in which CUDA-like concurrency primitives (shared memory synchronization, inter-thread communication, etc.) are inexistent."

[370] https://triton-lang.org/main/programming-guide/chapter-1/introduction.html, "Introduction, Triton documentation", n.d. Quote: "our compiler manages to apply a broad range of interesting optimization automatically"

[371] https://web.archive.org/web/20210728/https://openai.com/blog/triton/, "Introducing Triton: Open-Source GPU Programming for Neural Networks (archive.org copy of the OpenAI post)", 2021-07-28. Quote: "The purpose of Triton is to fully automate these optimizations, so that developers can better focus on the high-level logic of their parallel code."

[372] https://web.archive.org/web/20210728/https://openai.com/blog/triton/, "Introducing Triton: Open-Source GPU Programming for Neural Networks (archive.org copy of the OpenAI post)", 2021-07-28. Quote: "kernels are defined as decorated Python functions, and launched concurrently with different program_id ’s on a grid of so-called instances ."

[373] https://triton-lang.org/main/getting-started/tutorials/01-vector-add.html, "Vector Addition, Triton documentation", n.d. Quote: "Each torch.tensor object is implicitly converted into a pointer to its first element."

[374] https://dmlc.github.io/dlpack/latest/python_spec.html, "Python Specification for DLPack", n.d. Quote: "If possible, this must be zero-copy"

[375] https://dmlc.github.io/dlpack/latest/, "DLPack documentation", n.d. Quote: "In order for an ndarray system to interact with a variety of frameworks, a stable in-memory data structure is needed."

[376] https://raw.githubusercontent.com/triton-lang/triton/main/README.md, "Triton README", n.d. Quote: "The aim of Triton is to provide an open-source environment to write fast code at higher productivity than CUDA, but also with higher flexibility than other existing DSLs."

[377] https://www.eecs.harvard.edu/~htk/publication/2019-mapl-tillet-kung-cox.pdf, "Triton: An Intermediate Language and Compiler for Tiled Neural Network Computations", 2019-06-22. Quote: "Note that slicing tiles to retrieve scalars or sub-arrays is otherwise forbidden."

[378] https://triton-lang.org/main/programming-guide/chapter-1/introduction.html, "Introduction, Triton documentation", n.d. Quote: "The main challenge posed by our proposed paradigm is that of work scheduling, i.e., how the work done by each program instance should be partitioned for efficient execution on modern GPUs."

[379] https://web.archive.org/web/20210728/https://openai.com/blog/triton/, "Introducing Triton: Open-Source GPU Programming for Neural Networks (archive.org copy of the OpenAI post)", 2021-07-28. Quote: "Triton aims to be broadly applicable, and therefore does not automatically schedule work across SMs"

[380] https://web.archive.org/web/20210728/https://openai.com/blog/triton/, "Introducing Triton: Open-Source GPU Programming for Neural Networks (archive.org copy of the OpenAI post)", 2021-07-28. Quote: "CPUs and AMD GPUs are not supported at the moment, but we welcome community contributions aimed at addressing this limitation."

[381] https://docs.modular.com/mojo/vision, "Mojo vision", n.d. Quote: "Our vision is to unify this fragmented software stack with the one programming language developers can use to target all the diverse hardware"

[382] https://docs.modular.com/mojo/vision, "Mojo vision", n.d. Quote: "The tooling, UX, and predictability of these systems are very problematic and they are limited by the base language syntax."

[383] https://docs.modular.com/mojo/vision, "Mojo vision", n.d. Quote: "Another key design choice in Mojo is that it acts as syntactic sugar for MLIR."

[384] https://docs.modular.com/mojo/faq, "Mojo FAQ", n.d. Quote: "Mojo leverages LLVM-level dialects for the hardware targets it supports, and it uses other MLIR-based code-generation backends where applicable."

[385] https://docs.modular.com/mojo/manual/values/ownership.md, "Ownership, Mojo", n.d. Quote: "Every value has only one owner at a time."

[386] https://docs.modular.com/mojo/manual/values/ownership.md, "Ownership, Mojo", n.d. Quote: "The Mojo compiler implements a lifetime checker that ensures that values are not destroyed when there are outstanding references to those values."

[387] https://raw.githubusercontent.com/modular/modular/98333f260bbf490d0f2034e923b42b2cf83c85df/mojo/docs/faq.md, "Mojo FAQ (repository copy at commit 98333f2)", 2025-02-27. Quote: "Over time, Mojo will become much more interesting to the general Python community as it grows to be a superset of Python."

[388] https://docs.modular.com/mojo/roadmap, "Mojo roadmap", n.d. Quote: "Mojo may or may not evolve into a full superset of Python"

[389] https://docs.modular.com/mojo/vision, "Mojo vision", n.d. Quote: "our primary focus is on building features that unlock high-performance, portable compute"

[390] https://raw.githubusercontent.com/modular/modular/98333f260bbf490d0f2034e923b42b2cf83c85df/mojo/docs/faq.md, "Mojo FAQ (repository copy at commit 98333f2)", 2025-02-27. Quote: "We have commited to open-sourcing Mojo in 2026."

[391] https://docs.modular.com/mojo/faq, "Mojo FAQ", n.d. Quote: "Mojo is open source under the Apache License v2.0 with LLVM Exceptions."

[392] https://docs.modular.com/mojo/manual/gpu/fundamentals/, "GPU programming fundamentals, MAX", n.d. Quote: "Calculate the global thread index within the entire grid."

[393] https://docs.modular.com/mojo/manual/python/, "Python interoperability, Mojo", n.d. Quote: "This is 100% compatible because we use the CPython runtime without modification for full compatibility with existing Python libraries."

[394] https://docs.modular.com/mojo/manual/python/, "Python interoperability, Mojo", n.d. Quote: "you must declare which Mojo functions and types are available to be called from Python"

[395] https://docs.modular.com/mojo/faq, "Mojo FAQ (Modular)", n.d. Quote: "Mojo code is interoperable with C code."

[396] https://docs.modular.com/mojo/faq, "Mojo FAQ", n.d. Quote: "Mojo code is also interoperable with C++ code that uses extern "C""

[397] https://www.jsoftware.com/papers/tot1.htm, "Notation as a Tool of Thought", 1980-08. Quote: "Brevity is achieved by subordinating detail, and we will here consider three important ways of doing this: the use of arrays, the assignment of names to functions and variables, and the use of operators."

[398] https://www.nature.com/articles/s41586-020-2649-2, "Array programming with NumPy", 2020-09-16. Quote: "Owing to its in-memory data model, NumPy is currently unable to directly utilize such storage and specialized hardware."

[399] https://doc.rust-lang.org/nomicon/ffi.html, "Foreign Function Interface (The Rustonomicon)", n.d. Quote: "or foreign (e.g. C++) exceptions to cross an FFI boundary, that boundary must use the appropriate -unwind ABI string."

[400] https://rust-lang.github.io/rfcs/2945-c-unwind-abi.html, "RFC 2945: C-unwind ABI", 2019-04-03. Quote: "panic! will cause an abort if it would otherwise “escape” from a function defined with"

[401] https://www.lua.org/manual/5.4/manual.html, "Lua 5.4 Reference Manual", n.d. Quote: "Internally, Lua uses the C longjmp facility to handle errors."

[402] https://en.cppreference.com/w/c/language/restrict, "restrict type qualifier (cppreference)", n.d. Quote: "To avoid undefined behavior, the programmer must ensure that the aliasing assertions made by the restrict-qualified pointers are not violated."

[403] https://dmlc.github.io/dlpack/latest/, "Welcome to DLPack's documentation (DLPack)", n.d. Quote: "Already a standard with wide community adoption and support: NumPy CuPy PyTorch Tensorflow MXNet TVM mpi4py Paddle JAX Hidet"

[404] https://raw.githubusercontent.com/immunant/c2rust/master/README.md, "C2Rust README", n.d. Quote: "The output of `c2rust transpile` is unsafe and unidiomatic; it is merely the first step in a longer migration process."

[405] https://gcc.gnu.org/onlinedocs/gfortran/Interoperability-with-C.html, "Interoperability with C (The GNU Fortran Compiler)", n.d. Quote: "Note that array dimensions are reversely ordered in C and that arrays in C always start with index 0 while in Fortran they start by default with 1."

[406] https://pkg.go.dev/cmd/cgo, "cgo command (pkg.go.dev)", n.d. Quote: "C code may not keep a copy of a Go pointer after the call returns, unless the memory it points to is pinned with runtime.Pinner and the Pinner is not unpinned while the Go pointer is stored in C memory."

[407] https://docs.julialang.org/en/v1/manual/calling-c-and-fortran-code/, "Calling C and Fortran Code (Julia manual)", n.d. Quote: "Whenever you have created a pointer to Julia data, you must ensure the original data exists until you have finished using the pointer."

