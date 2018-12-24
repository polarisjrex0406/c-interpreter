#include "stdio.h"
#include "stdlib.h"
#include "memory.h"
#include "string.h"
#define DEBUG 1

int token; 			// current token
char *src, *old_src;		// pointer to src string
int poolsize;			// default size
int line;			// current line number

int *text, *old_text, *stack; 	// text segment, dump text segment, stack
char *data;			// data segment

int *pc, *bp, *sp, ax, cycle; 	// VM registers, program counter, base pointer, stack pointer (from high addr -> low addr), general-purpose registers (GPRs)

// Instructions supported (intel x86-based)
enum { 
	LEA, IMM, JMP, CALL, JZ, JNZ, ENT, ADJ, LEV, LI, LC, SI, SC, PUSH, 
	OR, XOR, AND, EQ, NE, LT, GT, LE, GE, SHL, SHR, ADD, SUB, MUL, DIV, MOD,
	OPEN, READ, CLOS, PRTF, MALC, MSET, MCMP, EXIT 
};

// Tokens and classes supported (last operator has the highest precedence)
// (e.g. = -> Assign, == -> Eq, != -> Ne)
// Most of the tokens are understandable, some of them that are not so intuitive are pointed out below :
// Glo -> Global Variable
// Fun -> Function
// Lan / Lor -> && / ||
// Brak -> Bracket

enum {
	Num = 128, Fun, Sys, Glo, Loc, Id,
	Char, Else, Enum, If, Int, Return, Sizeof, While,
	Assign, Cond, Lor, Lan, Or, Xor, And, Eq, Ne, Lt, Gt, Le, Ge, Shl, Shr, Add, Sub, Mul, Div, Mod, Inc, Dec, Brak
};


// Identifier
// It's basically a list of keywords / stuff we have at the moment, each of them is called an identifier for a variable / a keyword / a number.
//
// 			 Symbol table:
// ----+-----+----+----+----+-----+-----+-----+------+------+----
//  .. |Token|Hash|Name|Type|Class|Value|BType|BClass|BValue| ..
// ----+-----+----+----+----+-----+-----+-----+------+------+----
//     |<---            one single identifier           --->|
// 
// Token : the return mark of an identifer, we also include keywords if, while and etc. and give them a unique token
// Hash : the hash of an identifier used for comparision of identifiers
// Name : string of the name of the identifier
// Class : the class of identifier, (e.g. Local / Global / Number)
// Type : the type of identifier (Int, Char, Pointer ...)
// Value : the value of the identifer, if the identifier is a function, value is the address of the function
// BType / BClass / BValue : when a local identifier is identical with a global identifer, put global identifier in BType / BClass / BValue.


int token_val; 			// value of current token
int *current_id, *symbols;	// current parsed ID, the Symbol Table above

// Since we don't support struct, we use enum as an array instead.
enum {Token, Hash, Name, Type, Class, Value, BType, BClass, BValue, IdSize};

// Keywords
// Keywords like if, while, else, return are not normal identifiers, they have certain meanings.
// There are 2 options:
// 1. We resolve them in Lexical Analyser
// 2. We predefine a symbol table with keywords and essential information of what they do.
// In pcc, we use option 2.

int *idmain;

// types of variable / function supported
enum { CHAR, INT, PTR };

int basetype, expr_type;	// basetype : type of declartion of variable / function / type
				// Note : for type declaration, only enum is supported in pcc
				// expr_type : type of an expression

int index_of_bp;		// index of base pointer on the stack

void match ();
// match is a wrapper for next() with token exception quits. It is written after void next()

void next ();
// next is our lexical analyser

int eval ();
// eval is the VM we are using for pcc interpreter

void enum_declaration () {
	// parse enum [id] { a = 1, b = 2, c = 3 ... }
	int i;
	i = 0;
	while (token != '}') {
		if (token != Id) {
			printf("ERROR : invalid enum identifier %d at line %d\n", token, line);
			exit(-1);
		}
		next();
		if (token == Assign) {
			// enum [id] { a = 1 }
			next();
			if (token != Num) {
				printf("ERROR : invalid enum initialiser at line %d\n", line);
				exit(-1);
			}
			i = token_val;
			next();
		}

		current_id[Class] = Num;
		current_id[Type] = INT;
		current_id[Value] = i++;

		if (token == ',') next();
	}
}

void function_parameter () {
	int type;
	int params;
	params = 0;
	while (token != ')') {
		// type of parameters
		
		type = INT;
		if (token == Int) match(Int);
		else if (token == Char) {
			type = CHAR;
			match(Char);
		}

		// pointer
		while (token == Mul) {
			match(Mul);
			type = type + PTR;
		}

		// parameter name
		if (token != Id) {
			printf("ERROR : invalid parameter declartion at line %d\n", line);
			exit(-1);
		}
		if (current_id[Class] == Loc) {
			printf("ERROR : duplicate parameter declaration at line %d\n", line);
			exit(-1);
		}

		match(Id);

		// Store global variable elsewhere, could be optimised if we check what global variable are duplicates and store them accordingly
		current_id[BClass] = current_id[Class]; 
		current_id[Class] = Loc;
		current_id[BType] = current_id[Type];
		current_id[Type] = type;
		current_id[BValue] = current_id[Value];
		current_id[Value] = params++;

		if (token == ',') match(',');
	}
	
	// This is used for VM to indicate the new BP below in void function_declaration();
	index_of_bp = params + 1;
}

void function_body () {
	// In pcc, all declarations in a function shall be in front of all expressions
	// type func_name (...) {
	// 	1. local declaration	
	// 	2. statements
	// }
	
	int pos_local; 		// position of local variables on the stack
	int type;

	pos_local = index_of_bp;

	while ( (token == Int || token == Char) ) {
		// declare local variables
		basetype = (token == Int) ? INT : CHAR;
		match(token);

		while (token != ';') {
			type = basetype;
			while (token == Mul) {
				match(Mul);
				type = type + PTR;
			}
			if (token != Id) {
				// invalid declaration
				printf("ERROR : invalid local declaration at line %d\n", line);
				exit(-1);
			}
			if (current_id[Class] == Loc) {
				// duplicate declaration
				printf("ERROR : duplicate local declaration at line %d\n", line);
				exit(-1);
			}
			
			match(Id);
			
			// store local variables
			current_id[BClass] = current_id[Class];
			current_id[Class] = Loc;
			current_id[BType] = current_id[Type];
			current_id[Type] = type;
			current_id[BValue] = current_id[Value];
			current_id[Value] = ++pos_local;
			
			if (token == ',') match(',');
		}

		match(';');
	}

	*++text = ENT;
	*++text = pos_local - index_of_bp;

	while (token != '}') statement();

	*++text = LEV;
}




void function_declaration () {
	// |    ....       | 	high address
	// +---------------+
	// | arg: param_a  |    new_bp + 3
	// +---------------+
	// | arg: param_b  |    new_bp + 2
	// +---------------+
	// |return address |    new_bp + 1
	// +---------------+
	// | old BP        | <- new BP
	// +---------------+
	// | local_1       |    new_bp - 1
	// +---------------+
	// | local_2       |    new_bp - 2
	// +---------------+
	// |    ....       |  	low address
	//
	// In a function, the VM access the variable via new_bp pointer and the offset of a variable,
	// therefore, it is important for us to know the number of arguments as well as the offset for each argument.
	//
	// variable_decl ::= type {'*'} id { ',' {'*'} id } ';'
	// 
	// function_decl ::= type {'*'} id '(' parameter_decl ')' '{' body_decl '}'
	// 
	// parameter_decl ::= type {'*'} id {',' type {'*'} id}
	// 
	// body_decl ::= {variable_decl}, {statement}
	// 
	// statement ::= non_empty_statement | empty_statement
	// 
	// non_empty_statement ::= if_statement | while_statement | '{' statement '}'
	//                      | 'return' expression | expression ';'
	// 
	// if_statement ::= 'if' '(' expression ')' statement ['else' non_empty_statement]
	// 
	// while_statement ::= 'while' '(' expression ')' non_empty_statement
	
	match('(');
	function_parameter();
	match(')');
	
	match('{');
	function_body();
	// match('}'); 
	// Note : Intuitively, we need to match the right bracket } indicating the end of a function. 
	// However, if we consume that character here, the outer while loop going through the whole source code would not be able to know that the function has ended. 
	// Therefore, we leave the consumption of character } to the outer loop.
	
	current_id = symbols;
	while (current_id[Token]) {
		if (current_id[Class] == Loc) {
			// restore the information of global variables that are covered in 
			current_id[Class] = current_id[BClass];
			current_id[Type] = current_id[BType];
			current_id[Value] = current_id[BValue];
		}
		current_id = current_id + IdSize;
	}

}

void global_declaration () {
	// global_declaration ::= enum_decl | variable_decl | function_decl
	//
	// enum_decl ::= 'enum' [id] '{' id ['=' 'num'] {',' id ['=' 'num'} '}'
	//
	// variable_decl ::= type {'*'} id { ',' {'*'} id } ';'
	//
	// function_decl ::= type {'*'} id '(' parameter_decl ')' '{' body_decl '}'
	int type, i;

	basetype = INT;
	
	// if (token == SYMBOL) is a look before piece that checks which type it is.
	// e.g. if token is enum pcc knows it is a enumarator, if token is int, then we dont't know if it is a function or a varibale, then we need to take a look at a lookahead piece, if the lookahead piece gives us (, then it must be a function, otherwise its a varibale.

	// enum
	if (token == Enum) {
		// enum [id] { a = 10, b = 20, c = 30 ... }
		match(Enum);
		if (token != '{') match(Id);
		if (token == '{') {
			match('{');
			enum_declaration();
			match('}');
		}
		
		match(';');
		return;
	}

	// type information
	if (token == Int) match(Int);
	else if (token == Char) {
		match(Char);
		basetype = CHAR;
	}
	
	// variable declaration
	while ( (token != ';') && (token != '}') ) {
		type = basetype;
		
		// pointer that start with * 
		// could be int *x, **x, ***x, ...
		// for all pointers, there must be a base type, e.g int, then the more level there is, the more * there is
		// pcc does this by adding PTR to type
		while (token == Mul) {
			match(Mul);
			type = type + PTR; 
		}
		if (token != Id) {
			// declaration is invalid
			printf("ERROR : invalid global declaration on line %d\n", line);
			exit(-1);
		}
		if (current_id[Class]) {
			// identifier exsists in Symbol table
			printf("ERROR : duplicate global declaration on line %d\n", line);
			exit(-1);
		}
		
		match(Id);
		current_id[Type] = type;
		
		if (token == '(') {
			// function
			current_id[Class] = Fun;
			current_id[Value] = (int)(text + 1); // Value stores the memory address of the function
			function_declaration();
		} else {
			// variable
			current_id[Class] = Glo; 
			current_id[Value] = (int)data;
			data = data + sizeof(int);
		}

		if (token == ',') match(',');
	}
	
	next();
}


// Lexical Analyser
void next () {
	char *last_pos;
	int hash;
	
	while ( (token = *src) ) {
	// We have 2 options when encourted unknown char
	// 1. Point out the ERROR and Quit the whole interpreter
	// 2. Point out the ERROR and Go on
	// In pcc, we chose 2. The while loop skips unknown char as well as whitespaces.
		*src++;
		if 	(token == '\n') 	line++;
		else if (token == '#')
				// pcc does not support macros at this stage
				while ( (*src != 0) && (*src !='\n') ) src++;

		// Identifier	
		else if ( (token >= 'a' && token <= 'z') || (token >= 'A' && token <= 'Z') || (token == '_')) {
			
			// parse identifier
			last_pos = src - 1;
			hash = token;
			while ( (*src >= 'a' && src <= 'z') || (*src >= 'A' && *src <= 'Z') || (*src >= '0' && *src <= '9') || (*src == '_')) {
				hash = hash * 147 + *src;
				src++;
			}
			
			// search for identifer to see if there is already one
			// The following is a linear search, could be optimised
			current_id = symbols;
			while (current_id[Token]) {
				if ( (current_id[Hash] == hash) && (!memcmp( (char *)current_id[Name], last_pos, src - last_pos) ) ) {
					// there is one exsisting identifier already
					token = current_id[Token];
					return;
				}
				// Otherwise go on with the search
				current_id = current_id + IdSize;
			}

			// Not found, store a new id
			current_id[Name] = (int)last_pos;
			current_id[Hash] = hash;
			token = current_id[Token] = Id;
			return;
		}

		// Number
		else if (token >= '0' && token <= '9') {
			// parse number
			// There are 3 types of number supported in pcc : dec(123), hex(0x123), oct(01237)
			// Like in C99, decimal number 123 should not be written as 0123 <- which will be taken as a oct
			token_val = token - '0';
			if (token_val > 0) 
				// dec
				while (*src >= '0' && *src <= '9') token_val = token_val * 10 + *src++ - '0';
			else {
				if (*src == 'x' || *src == 'X') {
					// hex
					token = *++src;
					while ( (token >= '0' && token <= '9') || (token >= 'a' && token <= 'f') || (token >= 'A' && token <= 'F' )) {
						token_val = token_val * 16 + (token & 15) + (token >= 'A' ? 9 : 0);
						// In ASCII, a has hex value of 61, A has hex value of 41. Therefore, token & 15 gives us the value of the last digit.
						// This is written in a quite hacky way. However, you can rewrite this bit with if statements.
						token = *++src;
					}
				} else 
					// oct
					while (*src >= '0' && *src <= '7') token_val = token_val * 8 + *src++ - '0';
			}
		}
		
		// String
		// When encountering a string, we place it in data and returns the address of data. 
		// Also, pcc supports \n and \a, which is also dealt with as a string.
		else if (token == '"' || token == '\'') {
			last_pos = data;
			while ( (*src != 0) && (*src != token) ) {
				token_val = *src++;
				// deal with \n characters
				if (token_val == '\\') {
					token_val = *src++;
					if (token_val == 'n') token_val = '\n';
				}
				if (token == '"') *data++ = token_val;
			}

			src++;

			// if it is a single character, take it as char, return a number token
			if (token == '"') token_val = (int)last_pos;
			else token = Num;

			return;
		}

		// Comments
		// pcc only support // comments
		else if (token == '/') {
			if (*src == '/')
				// skipping whole commented line
				while ( (*src != 0) && (*src != '\n') ) src++;
			else {
				// its division
				token = Div;
				return;
			}
		}
		
		// =
		// = Assign
		// == Equal to
		else if (token == '=') {
			if (*src == '=') {
				src++;
				token = Eq;
			} else token = Assign;
			return;
		}
		
		// +
		// ++ Increase by 1
		// + Add
		else if (token == '+') {
			if (*src == '+') {
				src++;
				token = Inc;
			} else token = Add;
			return;
		}
		
		// -
		// -- Decrease by 1
		// - Substraction
		else if (token == '-') {
			if (*src == '-') {
				src++;
				token = Dec;
			} else token = Sub;
			return;
		}

		// !
		// != Not Equal
		else if (token == '!') {
			if (*src == '=') {
				src++;
				token = Ne;
			}
			return;
		}
		
		// <
		// <= Less than or Equal
		// << Left Shift
		// < Less than
		else if (token == '<') {
			if (*src == '=') {
				src++;
				token = Le;
			} else if (*src == '<') {
				src++;
				token = Shl;
			} else token = Lt;
			return;
		}

		// >
		// >= Greater than or Equal
		// >> Right Shift
		// > Greater than
		else if (token == '>') {
			if (*src == '=') {
				src++;
				token = Ge;
			} else if (*src == '>') {
				src++;
				token = Shr;
			} else token = Gt;
			return;
		}
		
		// |
		// |  binary or
		// || logic or
		else if (token == '|') {
			if (*src == '|') {
				src++;
				token = Lor;
			} else token = Or;
			return;
		}

		// &
		// &  binary and
		// && logic and
		else if (token == '&') {
			if (*src == '&') {
				src++;
				token = Lan;
			} else token = And;
			return;
		}

		else if (token == '^') {
			token = Xor;
			return;
		}
		else if (token == '%') {
			token = Mod;
			return;
		}
		else if (token == '*') {
			token = Mul;
			return;
		}
		else if (token == '[') {
			token = Brak;
			return;
		}
		else if (token == '?') {
			token = Cond;
			return;
		}
		else if (token == '~' || token == ';' || token == '{' || token == '}' || token == '(' || token == ')' || token == ']' || token == ',' || token == ':') 
			//token is what it is now
			return;

	}
	return;
}


void match(int tk) {
	if (token == tk) next();
	else {
		printf("ERROR : expected token %d at line %d\n", tk, line);
		exit(-1);
	}
}


void expression (int level) {
	
}

void program () {
	// get next token
	next();
	while (token > 0) {
		global_declaration();
	}
}

// VM
int eval () {
	int op, *tmp;
	cycle = 0;

	while (1) {
		// Get next command
		cycle++;
		op = *pc++;
            	
		if (DEBUG) {
			printf("cycle %d > %.4s", cycle,
					& 	"LEA ,IMM ,JMP ,CALL,JZ  ,JNZ ,ENT ,ADJ ,LEV ,LI  ,LC  ,SI  ,SC  ,PUSH,"
						"OR  ,XOR ,AND ,EQ  ,NE  ,LT  ,GT  ,LE  ,GE  ,SHL  SHR  ADD ,SUB ,MUL ,DIV ,MOD ,"
						"OPEN,READ,CLOS,PRTF,MALC,MSET,MCMP,EXIT"[op * 5]);
			if (op <= ADJ) printf(" pc = %d\n", *pc);
			else printf("\n");
		}

		//switch (op) {
			// Operations / Instructions

			// MOV
			// MOV dest, source (basically moving the stuff in source to destination, could be anything)
			// In pcc, we split MOV into 5 commands which only takes in at most 1 argument
			// IMM <num> : Put <num> to ax
			// LC : Load Char to ax addr
			// LI : Load Integer to ax addr
			// SC : Save Char from ax addr to Stack Top addr
			// SI : Save Integer from ax addr to Stack Top addr
			
			
			//case IMM :
			//	ax = *pc++;			//load immediate value to ax
			//	break;
			//case LC :
			//	ax = *(char *)ax;		//load char to ax addr;
			//	break;
			//case LI :
			//	ax = *(int *)ax;		//load int to ax addr;
			//	break;
			//case SC :
			//	ax = *(char *)*sp++ = ax;	//save char to stack top addr
			//	break;
			//case SI :
			//	*(int *)*sp++ = ax;		//save int to stack top addr
			//	break;
			
			
			if 	(op == IMM)	{ ax = *pc++; }
			else if (op == LC)	{ ax = *(char *)ax; }
			else if (op == LI)	{ ax = *(int *)ax; }
			else if (op == SC)	{ ax = *(char *)*sp++ = ax; }
			else if (op == SI)	{ *(int *)*sp++ = ax; }


			// PUSH
			// PUSH : push the value of ax to the stack
			
			
			//case PUSH :
			//	*--sp = ax;
			//	break;
			
			else if (op == PUSH)	{ *--sp = ax; }

			// JMP
			// JMP <addr> : set program counter to the new <addr>
			
			
			//case JMP :
			//	pc = (int *)*pc;		//pc is storing the next command, which is the new <addr> we want to JMP to.
			//	break;
			
			else if (op == JMP)	{ pc = (int *)*pc; }
			
			// JZ / JNZ
			// if statement is implement using JZ and JNZ (jump when is zero, jump when is not zero)
			// JZ : jump when ax is zero
			// JNZ : jump when ax is not zero
			 
			
			//case JZ :
			//	pc = ax ? pc + 1 : (int *)*pc;
			//	break;
			//case JNZ :
			//	pc = ax ? (int *)*pc : pc + 1;
			//	break;
			
			else if (op == JZ)	{ pc = ax ? pc + 1 : (int *)*pc; }
			else if (op == JNZ)	{ pc = ax ? (int *)*pc : pc + 1; }

			// Subroutine
			// CALL <addr> : call subroutine at <addr>. Note this is different from JMP since we need to store the current pc for future coming back to.
			// RET : return from subroutine (replaced by LEV)
			// ENT <size> : enter (make new call frame) stores the current stack pointer and saves <size> on stack to store local variables. 
			// ADJ <size> : (remove arguments from frame) removes the <size> amount of data in stack done by ENT
			// LEV : leave (restore call frame and pc) restore the stored call frame before ENT and restores pc
			// LEA : load address for arguments, a tmp fix for our ADD, since ADD only operates on ax, we need a command to operate the offset

			// sub_function(arg1, arg2, arg3)
			// |    ....       | high address
			// +---------------+
			// | arg: 1        |    new_bp + 4
			// +---------------+
			// | arg: 2        |    new_bp + 3
			// +---------------+
			// | arg: 3        |    new_bp + 2
			// +---------------+
			// |return address |    new_bp + 1
			// +---------------+
			// | old BP        | <- new BP
			// +---------------+
			// | local var 1   |    new_bp - 1
			// +---------------+
			// | local var 2   |    new_bp - 2
			// +---------------+
			// |    ....       |  low address
			
			
			//case CALL :
			//	*--sp = (int)(pc + 1);
			//	pc = (int *)*pc;
			//	break;

			////case RET :
			////	pc = (int *)*sp++;
			//// Above is an ideal RET function. In pcc, we use LEV to replace RET since we need to consider the return value of subroutines.
			//case ENT :
			//	*--sp = (int)bp;
			//	bp = sp;
			//	sp = sp - *pc++;
			//	break;
			//case ADJ :
			//	sp = sp + *pc++;
			//	break;
			//case LEV :
			//	sp = bp;
			//	bp = (int *)*sp++; 
			//	pc = (int *)*sp++;
			//	break;
			//case LEA :
			//	ax = (int)(bp + *pc++);
			//	break;
			//
			
			else if (op == CALL)	{ *--sp = (int)(pc + 1); pc = (int *)*pc; }
			else if (op == ENT)	{ *--sp = (int)bp; bp = sp; sp = sp - *pc++; }
			else if (op == ADJ)	{ sp = sp + *pc++; }
			else if (op == LEV)	{ sp = bp; bp = (int *)*sp++; pc = (int *)*sp++; }
			else if (op == LEA)	{ ax = (int)(bp + *pc++); }

			// Operator Instructions
			// These are built-in basic operations. 
			
			//case OR : 	ax = *sp++ | ax; 	break;
			//case XOR : 	ax = *sp++ ^ ax; 	break;
			//case AND : 	ax = *sp++ & ax; 	break;
			//case EQ : 	ax = *sp++ == ax;	break;
			//case NE :	ax = *sp++ != ax;	break;
			//case LT :	ax = *sp++ < ax;	break;
			//case LE :	ax = *sp++ <= ax;	break;
			//case GT :	ax = *sp++ > ax;	break;
			//case GE :	ax = *sp++ >= ax;	break;
			//case SHL :	ax = *sp++ << ax;	break;
			//case SHR :	ax = *sp++ >> ax;	break;
			//case ADD :	ax = *sp++ + ax;	break;
			//case SUB :	ax = *sp++ - ax;	break;
			//case MUL :	ax = *sp++ * ax;	break;
			//case DIV :	ax = *sp++ / ax;	break;
			//case MOD :	ax = *sp++ % ax;	break;	

			else if (op == OR)	{ ax = *sp++ | ax; }
			else if (op == XOR)	{ ax = *sp++ ^ ax; }
			else if (op == AND)	{ ax = *sp++ & ax; }
			else if (op == EQ)	{ ax = *sp++ == ax; }
			else if (op == NE)	{ ax = *sp++ != ax; }
			else if (op == LT)	{ ax = *sp++ < ax; }
			else if (op == LE)	{ ax = *sp++ <= ax; }
			else if (op == GT)	{ ax = *sp++ > ax; }
			else if (op == GE)	{ ax = *sp++ >= ax; }
			else if (op == SHL)	{ ax = *sp++ << ax; }
			else if (op == SHR)	{ ax = *sp++ >> ax; }
			else if (op == ADD)	{ ax = *sp++ + ax; }
			else if (op == SUB)	{ ax = *sp++ - ax; }
			else if (op == MUL)	{ ax = *sp++ * ax; }
			else if (op == DIV) 	{ ax = *sp++ / ax; }
			else if (op == MOD)	{ ax = *sp++ % ax; }
			
			// System Commands
			// These commands including open and closing files, IO from console, memory allocation and etc.
			// These commands requires extensive knowledge to implement, such that we will simply use built-in functions provided.
			
			//case EXIT :
			//	printf("EXIT : %d\n", *sp);
			//	return *sp;
			//	break;
			//case OPEN :
			//	ax = open( (char *)sp[1], sp[0]);
			//	break;
			//case CLOS :
			//	ax = close(*sp);
			//	break;
			//case READ :
			//	ax = read(sp[2], (char *)sp[1], *sp);
			//	break;
			//case PRTF :
			//	tmp = sp + pc[1];
			//	ax = printf( (char *)tmp[-1], tmp[-2], tmp[-3], tmp[-4], tmp[-5], tmp[-6]);
			//	break;
			//case MALC :
			//	ax = (int)malloc(*sp);
			//	break;
			//case MSET :
			//	ax = (int)memset( (char *)sp[2], sp[1], *sp);
			//	break;
			//case MCMP :
			//	ax = memcmp( (char *)sp[2], (char *)sp[1], *sp);
			//	break;
						
			else if (op == EXIT)	{ printf("EXIT : %d\n", *sp); return *sp; }
			else if (op == OPEN)	{ ax = open( (char *)sp[1], sp[0]); }
			
			else if (op == CLOS)	{ ax = close(*sp); }
			else if (op == READ) 	{ ax = read(sp[2], (char *)sp[1], *sp); }
			else if (op == PRTF)	{ tmp = sp + pc[1]; ax = printf( (char *)tmp[-1], tmp[-2], tmp[-3], tmp[-4], tmp[-5], tmp[-6]); }
			else if (op == MALC)	{ ax = (int)malloc(*sp); }
			else if (op == MSET) 	{ ax = (int)memset( (char *)sp[2], sp[1], *sp); }
			else if (op == MCMP) 	{ ax = memcmp( (char *)sp[2], (char *)sp[1], *sp); }
			
			// ERROR fallback
			// If op doesn't belong to any of the above instructions, there must be something wrong, therefore we exit the VM.
			
			
			//default :
			//	printf("ERROR : unknown instruction %d\n", op);
			//	return -1;
			//	break;
			
			else {
				printf("ERROR : unknown instruction %d\n", op);
				return -1;
			}
	}
	return 0;
}

int main (int argc, char **argv) {
	int i, fd;

	argc--;
	argv++;

	// default size && init
	poolsize = 256 * 1024; 
	line = 1;


	// read file && deal with ERROR msgs
	if ( (fd = open(*argv, 0)) < 0 ) {
		printf("ERROR : could not open file %s\n", *argv);
		return -1;
	}
	
	if ( !(src = old_src = malloc(poolsize)) ) {
		printf("ERROR : could not malloc size of %d for source area\n", poolsize);
		return -1;
	}

	if ( (i = read(fd, src, poolsize - 1)) <= 0) {
		printf("ERROR : read src failed; return value %d\n", i);
		return -1;
	}
	
	// add EOF
	src[i] = 0;
	close(fd);

	// read file finished, ready to do some real stuff
	// +------------------+
	// |    stack   |     |      high address
	// |    ...     v     |
	// |                  |
	// |                  |
	// |                  |
	// |                  |
	// |    ...     ^     |
	// |    heap    |     |
	// +------------------+
	// | bss  segment     |
	// +------------------+
	// | data segment     |
	// +------------------+
	// | text segment     |      low address
	// +------------------+
	//
	// Note : currently pcc does not support uninitialised variables, such that there would be no bss segment.


	// allocate memory for VM
	if ( !(text = old_text = malloc(poolsize)) ) {
		printf("ERROR : could not malloc size of %d for text area\n", poolsize);
		return -1;
	}

	if ( !(data = malloc(poolsize)) ) {
		printf("ERROR : could not malloc size of %d for data area\n", poolsize);
		return -1;
	}

	if ( !(stack = malloc(poolsize)) ) {
		printf("ERROR : could not malloc size of %d for stack area\n", poolsize);
		return -1;
	}
	
	// init value for VM
	memset(text, 0, poolsize);
	memset(data, 0, poolsize);
	memset(stack, 0, poolsize);
	
	bp = sp = (int *)( (int)stack + poolsize );
	ax = 0;
	

	src = "char else enum if int return sizeof while "
	      "open read close printf malloc memset memcmp exit void main";

	// add keywords to symbol table
	i = Char;
	while (i <= While) {
		next();
		current_id[Token] = i++;
	}

	// add Class / Type / Value to symbol table
	i = OPEN;
	while (i <= EXIT) {
		next();
		current_id[Class] = Sys;
		current_id[Type] = INT;
		current_id[Value] = i++;
	}

	next(); current_id[Token] = Char; // if void, pcc handle it as null char
	next(); idmain = current_id; // keep track of the main function


	program();
	return eval();
}
