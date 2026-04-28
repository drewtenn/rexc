// Canonical ANTLR grammar for Rexy source syntax.
grammar Rexy;

compilationUnit
	: item* EOF
	;

item
	: externFunction
	| staticBuffer
	| staticScalar
	| functionDefinition
	| moduleDeclaration
	| useDeclaration
	;

moduleDeclaration
	: 'pub'? 'mod' IDENT (';' | '{' item* '}')
	;

useDeclaration
	: 'use' qualifiedName ';'
	;

staticBuffer
	: 'pub'? 'static' 'mut'? IDENT ':' '[' primitiveType ';' INTEGER ']' ('=' staticArrayInitializer)? ';'
	;

staticScalar
	: 'pub'? 'static' 'mut'? IDENT ':' primitiveType '=' INTEGER ';'
	;

staticArrayInitializer
	: '[' (staticArrayElement (',' staticArrayElement)* ','?)? ']'
	;

staticArrayElement
	: '-'? INTEGER
	| BOOL
	| CHAR
	| STRING
	;

externFunction
	: 'pub'? 'extern' 'fn' IDENT '(' parameterList? ')' '->' type ';'
	;

functionDefinition
	: 'pub'? 'fn' IDENT '(' parameterList? ')' '->' type block
	;

parameterList
	: parameter (',' parameter)*
	;

parameter
	: IDENT ':' type
	;

type
	: '*' type
	| handleType
	| primitiveType
	;

handleType
	: IDENT ('<' type '>')?
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
	| indirectAssignStatement
	| incDecStatement
	| callStatement
	| returnStatement
	| ifStatement
	| matchStatement
	| whileStatement
	| forStatement
	| breakStatement
	| continueStatement
	;

letStatement
	: 'let' 'mut'? IDENT ':' type '=' expression ';'
	;

assignStatement
	: IDENT '=' expression ';'
	;

indirectAssignStatement
	: '*' expression '=' expression ';'
	;

incDecStatement
	: incrementExpression ';'
	;

callStatement
	: callExpression ';'
	;

returnStatement
	: 'return' expression ';'
	;

ifStatement
	: 'if' expression block ('else' (block | ifStatement))?
	;

matchStatement
	: 'match' expression '{' matchArm* '}'
	;

matchArm
	: matchPattern ('|' matchPattern)* '=>' block ','?
	;

matchPattern
	: '_'
	| '-'? INTEGER
	| BOOL
	| CHAR
	;

whileStatement
	: 'while' expression block
	;

forStatement
	: 'for' forInitializer expression ';' forIncrement block
	| 'for' '(' forInitializer expression ';' forIncrement ')' block
	;

forInitializer
	: letStatement
	| assignStatement
	;

forIncrement
	: IDENT '=' expression
	| '*' expression '=' expression
	| incrementExpression
	;

incrementExpression
	: ('++' | '--') IDENT
	| IDENT ('++' | '--')
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
	: cast (('*' | '/' | '%') cast)*
	;

cast
	: unary ('as' type)*
	;

unary
	: ('++' | '--') IDENT
	| '-' unary
	| '!' unary
	| '&' unary
	| '*' unary
	| postfix
	;

postfix
	: primary ('[' expression ']')* ('++' | '--')?
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
	: qualifiedName '(' argumentList? ')'
	;

qualifiedName
	: IDENT ('::' IDENT)*
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
