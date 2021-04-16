%include optional.i

%module parse_tree
%{
    #include <ParseTree.hpp>
%}

%std_optional(ParseNode);

%include <ParseTree.hpp>