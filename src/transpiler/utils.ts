// // export const isOfClass = (any[]) : boolean => {
// //     for (let i = 0; i < any.length; i++) {
// //         if (any[i].kind === 'CLASS') {
// //             return true;
// //         }
// //     }

// function isInstanceOfAny(obj, classes) {
//     return classes.some(cls => obj instanceof cls);
//   }
  
//   class A {}
//   class B {}
//   class C {}
  
//   const obj1 = new A();
//   const obj2 = new B();
//   const obj3 = {}; // Not an instance of any class
  
//   class D extends A {}
//   const obj4 = new D()
  
//   console.log(isInstanceOfAny(obj1, [A, B, C])); // true
//   console.log(isInstanceOfAny(obj2, [A, B, C])); // true
//   console.log(isInstanceOfAny(obj3, [A, B, C])); // false
//   console.log(isInstanceOfAny(obj4, [A, B, C])); // false
  