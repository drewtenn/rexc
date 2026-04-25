// Canonical ANTLR grammar for Rexc source syntax.
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
	| assignStatement
	| returnStatement
	| ifStatement
	| whileStatement
	| breakStatement
	| continueStatement
	;

letStatement
	: 'let' 'mut'? IDENT ':' type '=' expression ';'
	;

assignStatement
	: IDENT '=' expression ';'
	;

returnStatement
	: 'return' expression ';'
	;

ifStatement
	: 'if' expression block ('else' block)?
	;

whileStatement
	: 'while' expression block
	;

breakStatement
	: 'break' ';'
	;

continueStatement
	: 'continue' ';'
	;

expression
	: logicalOr
	;

logicalOr
	: logicalAnd ('||' logicalAnd)*
	;

logicalAnd
	: comparison ('&&' comparison)*
	;

comparison
	: additive (('==' | '!=' | '<' | '<=' | '>' | '>=') additive)*
	;

additive
	: multiplicative (('+' | '-') multiplicative)*
	;

multiplicative
	: cast (('*' | '/') cast)*
	;

cast
	: unary ('as' type)*
	;

unary
	: '-' unary
	| '!' unary
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
