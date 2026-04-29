// Canonical ANTLR grammar for Rexy source syntax.
grammar Rexy;

compilationUnit
	: item* EOF
	;

item
	: externFunction
	| staticBuffer
	| staticScalar
	| structDeclaration
	| enumDeclaration
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

structDeclaration
	: 'pub'? 'struct' IDENT genericParameters? '{' (structField (',' structField)* ','?)? '}'
	;

genericParameters
	: '<' IDENT (',' IDENT)* ','? '>'
	;

structField
	: IDENT ':' type
	;

enumDeclaration
	: 'pub'? 'enum' IDENT '{' enumVariant (',' enumVariant)* ','? '}'
	;

enumVariant
	: IDENT ('(' enumPayloadTypeList ')')?
	;

enumPayloadTypeList
	: type (',' type)* ','?
	;

externFunction
	: 'pub'? 'extern' 'fn' IDENT '(' parameterList? ')' '->' type ';'
	;

functionDefinition
	: 'pub'? 'unsafe'? 'fn' IDENT genericParameters? '(' parameterList? ')' '->' type block
	;

parameterList
	: parameter (',' parameter)*
	;

parameter
	: IDENT ':' type
	;

type
	: '*' type
	| sliceType
	| tupleType
	| handleType
	| primitiveType
	;

sliceType
	: '&' '[' type ']'
	;

tupleType
	: '(' type ',' type (',' type)* ','? ')'
	;

handleType
	: IDENT ('<' type (',' type)* ','? '>')?
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
	| fieldAssignStatement
	| incDecStatement
	| callStatement
	| returnStatement
	| ifStatement
	| matchStatement
	| whileStatement
	| forStatement
	| breakStatement
	| continueStatement
	| deferStatement
	| unsafeBlock
	;

unsafeBlock
	: 'unsafe' '{' statement* '}'
	;

deferStatement
	: 'defer' callExpression ';'
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

fieldAssignStatement
	: '(' '*' expression ')' '.' IDENT '=' expression ';'
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
	| qualifiedName '(' patternBindingList? ')'
	| IDENT '{' patternBindingList? '}'
	;

patternBindingList
	: patternBinding (',' patternBinding)* ','?
	;

patternBinding
	: IDENT
	| '_'
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
	: primary postfixSuffix* ('++' | '--')?
	;

postfixSuffix
	: '[' expression ']'
	| '.' (IDENT | INTEGER)
	| '?'
	;

primary
	: INTEGER
	| BOOL
	| CHAR
	| STRING
	| structLiteral
	| tupleExpression
	| callExpression
	| IDENT
	| '(' expression ')'
	;

tupleExpression
	: '(' expression ',' expression (',' expression)* ','? ')'
	;

structLiteral
	: IDENT ('::' '<' type (',' type)* ','? '>')? '{' (structLiteralField (',' structLiteralField)* ','?)? '}'
	;

structLiteralField
	: IDENT ':' expression
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
