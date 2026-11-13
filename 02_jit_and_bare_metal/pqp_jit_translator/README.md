# PQP JIT Translator

Este projeto implementa um tradutor JIT para a maquina virtual PQP. O programa le uma memoria de entrada com instrucoes de 4 bytes, gera codigo nativo x86_64 sob demanda e produz ao final um trace com a ordem de traducao, a frequencia de execucao e o estado final dos registradores.

## Visao geral

O arquivo principal e [rafaelgomes_202300095730_pqp.c](rafaelgomes_202300095730_pqp.c) e faz todo o trabalho sozinho:

1. Abre um arquivo de entrada com o programa em formato hexadecimal.
2. Carrega esses bytes em uma memoria virtual chamada `PICO_MEMO`.
3. Monta uma area executavel com um bootstrap inicial em x86_64.
4. Instala handlers para `SIGTRAP` e `SIGSEGV`.
5. Executa o bootstrap, que pula para o primeiro slot de traducao.
6. Quando a CPU encontra um slot nao traduzido, o handler compila a instrucao correspondente.
7. O codigo traduzido e executado imediatamente.
8. Quando o PC sai da faixa valida da memoria de programa, o programa encerra e imprime o resultado.

## Dependencias e ambiente

O codigo foi escrito para Linux x86_64 com extensoes GNU de C. Ele depende de:

- `mmap` com `MAP_ANONYMOUS` e `MAP_32BIT`
- `sigaction`, `sigaltstack` e `ucontext.h`
- registradores de contexto da ABI x86_64
- execucao de memoria com permissao `PROT_EXEC`

Por usar `MAP_32BIT` e deslocamentos relativos de 32 bits no codigo gerado, a execucao e pensada para ambientes onde o mapeamento abaixo de 2 GB e suportado.

## Formato da entrada

O arquivo de entrada e lido linha por linha. Cada linha deve conter exatamente quatro valores hexadecimais:

```text
opcode byte1 byte2 byte3
```

Na pratica, cada linha representa uma instrucao de 4 bytes:

- byte 0: opcode
- byte 1: registradores `rD` e `rS`
- byte 2: parte baixa da imediata de 16 bits
- byte 3: parte alta da imediata de 16 bits, ou deslocamento extra em shift

O programa carrega esses bytes em `PICO_MEMO` sequencialmente. O limite logico da memoria de programa e `256` bytes, isto e, `64` instrucoes de 4 bytes.

## Layout de memoria

O codigo separa a execucao em duas areas principais:

- `PICO_MEMO`: memoria da maquina virtual, alocada com `0x10000` bytes.
- `jit.code`: area executavel que guarda o bootstrap e os slots traduzidos.

Dentro de `jit.code` existe a seguinte organizacao:

- `0 .. 127`: bootstrap inicial
- `128 ..`: slots fixos de `32` bytes por instrucao traduzida

Cada slot corresponde a um PC da maquina virtual. Quando um slot e compilado, ele passa a conter o codigo nativo daquela instrucao, mais um preenchimento com `NOP` ou um salto curto para completar exatamente `32` bytes.

## Como o JIT funciona

O mecanismo central e baseado em interrupcao por execucao de bytes `0xCC`.

### 1. Bootstrap

Ao iniciar, `build_bootstrap()` gera codigo nativo que:

- zera todos os registradores gerais x86_64 usados pelo mapeamento
- reseta o FPU
- zera os 16 registradores XMM
- salta para o primeiro slot de traducao

Como a area de slots e inicializada com `0xCC`, o primeiro acesso a um slot ainda nao compilado dispara um `SIGTRAP`.

### 2. Handler de traducao

O handler `jit_compile()` recebe o contexto da interrupcao, descobre o slot atual a partir de `RIP` e calcula o PC da maquina virtual:

```text
PC = slot_idx * 4
```

Se o PC for maior ou igual a `PQP_MEM_SIZE`, o programa encerra com `do_exit()`.

Caso contrario, o handler:

- registra que aquele PC foi visitado
- posiciona o cursor de escrita no slot correspondente
- injeta uma instrucao de profiler baseada em XMM
- traduz a instrucao PQP para x86_64
- completa o slot com `NOP` ou um salto curto
- devolve o controle ao mesmo endereco, agora apontando para o codigo nativo gerado

### 3. Execucao do slot

Depois de traduzido, o slot passa a ser executado diretamente pelo processador. Isso significa que a traducao acontece uma vez por instrucao, e as execucoes seguintes reutilizam o codigo gerado.

## Profiler e contagem de execucao

Antes de traduzir cada instrucao, o programa injeta uma pequena rotina de profiler usando registradores vetoriais.

Essa rotina incrementa contadores armazenados em `vector_lanes`, permitindo que o final da execucao mostre quantas vezes cada grupo de instrucoes foi executado.

O contador e usado no `do_exit()` para ordenar e imprimir o trace final.

## Conjunto de instrucoes suportadas

Cada instrucao PQP tem 4 bytes. O `opcode` define a operacao, e `byte1` define os registradores `rD` e `rS`:

- `rD = byte1 >> 4`
- `rS = byte1 & 0x0F`

### `0x00` - `mov imm`

Carrega uma imediata de 16 bits com sinal em `rD`.

Formato:

- `rD <- sign_extend(imm16)`

### `0x01` - `mov r`

Move o conteudo de `rS` para `rD`.

### `0x02` - `load`

Le da memoria virtual:

- `rD <- [PICO_MEMO + rS]`

### `0x03` - `store`

Escreve na memoria virtual:

- `[PICO_MEMO + rD] <- rS`

### `0x04` - `cmp`

Compara `rD` com `rS` usando a semantica do `cmp` nativo.

### `0x05` - `jmp`

Desvio incondicional relativo ao proximo PC.

### `0x06` - `jg`

Desvio condicional se `rD > rS` de acordo com as flags geradas pelo `cmp` anterior.

### `0x07` - `jl`

Desvio condicional se `rD < rS`.

### `0x08` - `je`

Desvio condicional se `rD == rS`.

### `0x09` - `add`

Soma `rS` em `rD`.

### `0x0A` - `sub`

Subtrai `rS` de `rD`.

### `0x0B` - `and`

Operacao bit a bit AND entre `rD` e `rS`.

### `0x0C` - `or`

Operacao bit a bit OR entre `rD` e `rS`.

### `0x0D` - `xor`

Operacao bit a bit XOR entre `rD` e `rS`.

### `0x0E` - `sal`

Shift aritmetico/logic a esquerda em `rD`.

O deslocamento vem dos 4 bits baixos de `byte3`:

- `shift = byte3 & 0x0F`

### `0x0F` - `sar`

Shift aritmetico a direita em `rD`.

Tambem usa os 4 bits baixos de `byte3`.

## Como os desvios sao calculados

Para `jmp`, `jg`, `jl` e `je`, o destino e calculado assim:

```text
PC_target = (PC_next + sign_extend(imm16)) & 0xFFFF
```

Onde `PC_next = PC + 4`.

Se o destino estiver fora do intervalo esperado da maquina virtual, o emissor de branch calcula um desvio que leva para fora da regiao de slots, forcando a saida da execucao.

## Saida final

Quando a execucao termina, `do_exit()` produz um trace ordenado por PC e depois imprime os registradores finais.

O formato geral e:

```text
<contador>:<pc>:<instrucao>
...
-:<pc>:exit
r0=........,r1=........,...,r15=........
```

Em resumo, a saida contem:

- a frequencia de execucao de cada PC visitado
- a instrucao traduzida em texto legivel
- o PC que causou a saida
- o estado final de `r0` a `r15`

## Fluxo completo de execucao

1. `main()` abre o arquivo de entrada e o arquivo de saida.
2. `setup_signals()` instala os handlers de interrupcao e a stack alternativa.
3. `PICO_MEMO` e alocada com `mmap`.
4. O programa carrega as instrucoes hexadecimal por linha.
5. `jit.code` e alocado como memoria executavel e preenchido com `0xCC`.
6. `vector_lanes` e inicializado como matriz identidade para contadores.
7. `build_bootstrap()` gera o bootstrap inicial.
8. O ponteiro `jit.code` e chamado como funcao.
9. A primeira execucao de um slot nao traduzido dispara o handler.
10. O handler traduz a instrucao, grava o slot e retorna a execucao.
11. Ao sair da area valida de instrucoes, `do_exit()` imprime o resultado e finaliza.

## Observacoes importantes

- O programa assume ambiente x86_64 Linux.
- O comportamento depende de handlers de sinal e de execucao de memoria.
- A memoria de programa logica e pequena por projeto: apenas `256` bytes.
- O codigo nao faz validacao robusta de limites do arquivo de entrada.
- O arquivo de saida precisa ser gravavel, pois o programa escreve o trace final diretamente nele.

## Exemplo de uso

```bash
gcc -std=gnu11 -O2 -Wall -Wextra rafaelgomes_202300095730_pqp.c -o pqp
./pqp pqp.input.txt meuresultado.txt
```

## Resumo

Este codigo nao e um interpretador tradicional. Ele usa um JIT orientado por sinais para traduzir instrucoes PQP para x86_64 sob demanda, executar esse codigo nativo e gerar ao final um relatorio detalhado de execucao. O resultado e um simulador rapido, instrumentado e totalmente auto-contido em um unico arquivo C.