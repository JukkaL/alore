Class examples
--------------

These examples show how to define classes. They also show how to define 
modules, and some of them use advanced type system features rarely used by
normal application code.

complex/
  A statically-typed that defines an almost minimal complex number class.
complex.alo
  A simple script that uses the "complex" module.

complex2/
  A more advanced example of defining a complex number class. Unlike the
  first example, these complex numbers can be used in mixed type-safe
  arithmetic operations with Float objects.
complex2.alo:
  A simple script that uses the "complex2" module.
  
test-complex2.alo:
  Unit test example for complex numbers, defined using the unittest module.
  The unit tests are dynamically-typed while the module that they test uses
  static typing.
