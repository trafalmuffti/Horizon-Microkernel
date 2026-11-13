# Horizon Microkernel — working notes

## Guiding principle

**The point of an operating system is to run other code.**

Everything else — a network stack, a filesystem, a clock, a driver model — is
means, not end. A feature that cannot be reached by a userspace program is not
yet a feature.

Practical consequences for work in this repository:

- **Fix the path to running code before improving anything along it.** If no
  userspace program can execute, accuracy, performance and polish of the
  things it would have called are worth nothing. Order work by what unblocks
  execution.
- **Prefer a result a program can observe** over one that only appears in
  kernel debug output. `dtrace` proving a subsystem initialised is weaker
  evidence than a userspace binary using it and printing something.
- **A feature is done when a program uses it end to end**, not when it
  compiles and not when it links.

## Verification

Claims about this repository should rest on something that was run, not on
reading. Compiling proves the code is well-formed; booting it under QEMU
proves rather more. When something has only been compiled, say so rather than
implying it ran.

Do not present illustrative or placeholder output as though it were a captured
result.
