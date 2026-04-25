grammar Rexc;

compilationUnit
	: item* EOF
	;

item
	: externFunction
	| functionDefinition
	;

externFunction
	: 'extern' 'fn' IDENT '(' parameterList? ')' '->' type ';'
	;

functionDefinition
	: 'fn' IDENT '(' parameterList? ')' '->' type block
	;

parameterList
	: parameter (',' parameter)*
	;

parameter
	: IDENT ':' type
	;

type
	: 'i32'
	;

block
	: '{' statement* '}'
	;

statement
	: letStatement
	| returnStatement
	;

letStatement
	: 'let' IDENT ':' type '=' expression ';'
	;

returnStatement
	: 'return' expression ';'
	;

expression
	: additive
	;

additive
	: multiplicative (('+' | '-') multiplicative)*
	;

multiplicative
	: primary (('*' | '/') primary)*
	;

primary
	: INTEGER
	| callExpression
	| IDENT
	| '(' expression ')'
	;

callExpression
	: IDENT '(' argumentList? ')'
	;

argumentList
	: expression (',' expression)*
	;

IDENT
	: [a-zA-Z_][a-zA-Z0-9_]*
	;

INTEGER
	: [0-9]+
	;

WS
	: [ \t\r\n]+ -> skip
	;

LINE_COMMENT
	: '//' ~[\r\n]* -> skip
	;
