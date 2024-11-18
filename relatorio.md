## Relatório Compiladores

O tema escolhido para o trabalho de compiladores foi BPMN (Bussiness Process Model and Notation). Através do uso de uma linguagem de marcação semelhante ao XML/HTML, será possível criar um modelo gráfico. A linguagem escolhida para desenvolvimento foi C e, como biblioteca gráfica, foi utilizado Raylib.

Na linguagem teremos alguns conceitos chaves, sendo eles:
- Processos: O modelo principal que esta sendo descrito. Cada arquivo deverá possuir uma e apenas uma declaração de processo;
- Subprocessos: Um arquivo poderá possuir diversos subprocessos e ele representa etapas do processo principal;
- Eventos: Um subprocesso será descrito através de diferentes eventos.

O seguinte exemplo define um processo para fazer macarrão, possuindo eventos em dois subprocessos: o do mercado e o da cozinha.

Exemplo de entrada:

Exemplo de saída:


### Analisador léxico

O análisador foi definido para reconher os seguintes tokens:

TOKEN_OPTAG := "<"
TOKEN_CLTAG := ">"
TOKEN_ATR := "="
TOKEN_SLASH := /
TOKEN_ID := [a-zA-Z0-9_]+
TOKEN_STR := '[\w\S]*'
TOKEN_TYPE := "task" | "gateway" | "starter" | "end"
TOKEN_PROCESS := "process"
TOKEN_SUBPROCESS := "subprocess"
TOKEN_EVENTS := "events"
TOKEN_COL := "col"

Para o reconhecimento dos tokens, o lexer lê cada caracter da entrada e faz as seguintes verificações:

Se está lendo o primeiro caractere do token:
    o caractere é <? Retorna TOKEN_OPTAG
    o caractere é >? Retorna TOKEN_CLTAG
    o caractere é =? Retorna TOKEN_ATR
    o caractere é /? Retorna TOKEN_SLASH
    o caractere é '?
        continue lendo até encontrar o próximo ' e retorna TOKEN_STR
    o caractere é alfanúmerico?
        continue lendo até encontrar um espaço
            o valor lido é "task" ou "gateway" ou "stater" ou "end"? Retorna TOKEN_TYPE
            o valor lido é "process"? Retorna TOKEN_PROCESS
            o valor lido é "subprocess"? REtorna TOKEN_SUBPROCESS
            o valor lido é "events"? Retorna TOKEN_EVENTS
            o valor lido é "colr"? Retorn TOKEN_COL

No analisador léxico, além do código que está sendo interpretado e a localização do cursor, algumas outras estrutura são armazeadas por conveniência, como a tabela de simbolos, o último token lido e uma tabela com as palavras reservadas.

```c
typedef struct {
    char *content;
    size_t col, row;
    const char *file_path;
    Hash_Map symbols;
    Keyword_Table keyword_table;
    Token token;
} Lexer;
```

### Parser
O parsing do código fornecido é feito através do uso de tradução dirigida por sintaxe e análise preditiva. Segue a especificação da grámatica:

```
processo ::= '<process name=' str '>' lista_subprocesso '</process>'

lista_subprocesso ::= subprocesso |
                      subprocesso lista_subprocesso

subprocesso ::= '<subprocess' lista_atribuicao '>' eventos '</subprocess>'

eventos ::= '<events>' lista_evento '</events>'

lista_evento ::= evento |
                 evento lista_evento

evento ::= evento_tipo |
           coluna

evento_tipo ::=  '<starter' lista_atribuicao '/>' |
                 '<task' lista_atribuicao '/>'    |
                 '<gateway' lista_atribuicao '/>' |
                 '<end' lista_atribuicao '/>'

coluna ::= '<col>' lista_evento '</col>' | '<col/>'

lista_atribuicao ::= atribuicao |
                     atribuicao lista_atribuicao

atribuicao ::= id '=' str

id ::= [a-zA-Z0-9_]+

str ::= "'" .* "'"

```