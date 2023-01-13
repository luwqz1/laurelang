!use std:abstract.

-- <-inf..inf>
: #test_abstract_primitive(int).
#test_abstract_primitive(x) {
    int p = <0..3;
    arr ~ p[];
    abstract_primitive(arr) = x;
}

-- [<-inf..inf> x 0..inf>]
: #test_abstract_nesting(int[]).
#test_abstract_nesting(x) {
    p > 0;
    arr ~ p[][];
    abstract_nesting(x) = arr;
}