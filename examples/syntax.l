-- this code is laurelang syntax declaration
-- this is comment.

*--
this is multiline comment
generics can be clarified before header declaration
headers can be grouped by their common used generic group
--*
{T: union<int, char>, R: optional<T>}: 
    ?transpose('T'[x][y]) -> 'R'[y][x],
    ?transposed_element('T'[x][y], index_t) -> 'R'[y].

*-- this is complex predicate declaration
in first line elements are declared.
Variables from header are freely accessible.
This lines will be considered the documentation of the predicate.
It can contain **markdown** and documentation can be accessed via REPL
with command `.doc transposed_element`.
--*
?transposed_element(arr, idx) -> el {
    i ~ int;
    -- getting by idx will check whether such element exists
    arr[i][idx] -> {
        el[idx] = arr[i][idx];
    };
}

?transpose(arr) -> tzd {
    -- * means getting a primitive (shortcut to <abstract>::abstract_primitive)
    t ~ (**tzd)[y][x];
    -- implication here means that statement must be true
    -- for each arr element with index [i][j]
    arr[i][j] -> {
        t[i] = transposed_element(arr, i);
    }
    tzd = t;
}

-- structure can define multiple cases
-- this is internal declaration of union
$union('A', 'B') {
    'A' data;
} | {
    'B' data;
}

-- internal declaration of union
$optional('X') {
    union<'X', @nothing> data;
}

I ~ int. -- this is imaging. I is now constant locked `int`
-- constraints can be applied
I > 3.
-- but not predicates!
-- I = 1 + 3. -- forbidden

I = 3. -- forbidden because of constraint! consult will fail
I = 4. -- this is assertion

int J = 100. -- this is declaration, two expressions in one: imaging and assertion.
string S = "This is a string".

-- this is a **use** statement. It uploads statements from other source.
!use std:datetime.

-- this is an atomic multiplicity declaration
sex ~ @{male, female}.

*-- this is a predicate with auto resolved case-specific **particle**.
ID is resolved from **datalink** or in runtime on predicate initialization.
Each case of the predicate will have its unique ID, but it doesn't matter how much
choicepoints case will generate. ID is same for solutions generated by same predicate case.
--*
: ?person(string, sex, date) -> ID.

?person("Alexander", @male, (05, 10, 2001)).
?person("Joseph", @male, (13, 06, 1952)).
?person("Selma", @female, (18, 03, 1993)).
?person("Akiv", @female, (27, 05, 2005)).

*-- the benefits of predicate with ID is that we can easily access any values.
The predicate below will determine the age of the person.
`_` is an anonymous variable, it is not resolved during solution search.
--*
?age(person) -> natural*. -- `person` as a predicate with ID can be used as a type.
-- `age` will take person's ID as an argument, but its easy to resolve just calling this predicate.
-- for example: `?- x = age(person("Selma", _, _))`

?age(person(_, _, dob)) -> years {
    -- we will use date_subtract predicate from `datetime` package.
    delta = date_subtract(today, dob);
    delta = (_, _, years); -- years are resolved from delta (as well this predicate can be used the other way)
}

-- with age predicate we can find the age of the person
-- as well as all the people of some age (`?- age(person(name, _, _)) = 70`)
-- or age in domain (`?- age(person(name, _, _)) = 18..22`)