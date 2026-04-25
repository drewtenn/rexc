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
	: primitiveType
	;

primitiveType
	: 'i8'
	| 'i16'
	| 'i32'
	| 'i64'
	| 'u8'
	| 'u16'
	| 'u32'
	| 'u64'
	| 'bool'
	| 'char'
	| 'str'
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
	: unary (('*' | '/') unary)*
	;

unary
	: '-' unary
	| primary
	;

primary
	: INTEGER
	| BOOL
	| CHAR
	| STRING
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

BOOL
	: 'true'
	| 'false'
	;

IDENT
	: [a-zA-Z_][a-zA-Z0-9_]*
	;

INTEGER
	: [0-9]+
	;

CHAR
	: '\'' (ESCAPE | ~['\\\r\n]) '\''
	;

STRING
	: '"' (ESCAPE | ~["\\\r\n])* '"'
	;

fragment ESCAPE
	: '\\' [nrt'"\\]
	;

WS
	: [ \t\r\n]+ -> skip
	;

LINE_COMMENT
	: '//' ~[\r\n]* -> skip
	;
