.intel_syntax noprefix

.section .note.GNU-stack, "", @progbits

.section .data
    fmt_hex:        .asciz "%x"             # para paleta, x, y e b
    fmt_dec:        .asciz "%d"             # para qtd imagens e m
    fmt_char:       .asciz "%c"
    fmt_newline:    .asciz "\n"
    fmt_comp_rate:  .asciz "[%d.%02d%%]\n"

    mode_r:         .asciz "r"              
    mode_w:         .asciz "w"              

.section .bss
    .lcomm palette, 16              
    
    .lcomm canvas, 2000000          # 2 megabytes de matriz 2d
    .lcomm triplas_x, 2000000       # posicoes suficientes para ficheiros grandes
    .lcomm triplas_y, 2000000         
    .lcomm triplas_b, 2000000         
    
    .lcomm temp_val, 4
    .lcomm max_b_val, 4             # guarda o b do maior par (y, x)
    .lcomm qtd_imagens, 4
    .lcomm expected_m, 4            

    .lcomm inputF, 8                
    .lcomm outputF, 8               

.section .text
.global main

.macro LER_TEMP_DO_INPUT fmt_label
    MOV rdi, QWORD PTR [rip + inputF]
    LEA rsi, [rip + \fmt_label]
    LEA rdx, [rip + temp_val]
    XOR eax, eax
    CALL fscanf@plt
.endm

.macro SALVAR_TEMP_NA_TRIPLA triple_label, idx_reg
    MOV eax, DWORD PTR [rip + temp_val]
    LEA r8, [rip + \triple_label]
    MOV DWORD PTR [r8 + \idx_reg*4], eax
.endm

.macro LER_E_SALVAR_TRIPLA fmt_label, triple_label, idx_reg, fail_lbl
    MOV rdi, QWORD PTR [rip + inputF]
    LEA rsi, [rip + \fmt_label]
    LEA rdx, [rip + temp_val]
    XOR eax, eax
    CALL fscanf@plt
    CMP eax, 1
    JNE \fail_lbl
    MOV eax, DWORD PTR [rip + temp_val]
    LEA r8, [rip + \triple_label]
    MOV DWORD PTR [r8 + \idx_reg*4], eax
.endm

main:
    # prologo 
    PUSH rbp
    MOV rbp, rsp
    PUSH rbx
    PUSH r12
    PUSH r13
    PUSH r14
    PUSH r15
    SUB rsp, 8                      

    CMP rdi, 3                      
    JL .fim_programa_sem_fechar     

    MOV rbx, rsi                    

    MOV rdi, QWORD PTR [rbx + 8]    
    LEA rsi, [rip + mode_r]         
    CALL fopen@plt
    CMP rax, 0                      
    JE .fim_programa_sem_fechar
    MOV QWORD PTR [rip + inputF], rax

    MOV rdi, QWORD PTR [rbx + 16]   
    LEA rsi, [rip + mode_w]         
    CALL fopen@plt
    CMP rax, 0
    JE .fechar_input_e_sair
    MOV QWORD PTR [rip + outputF], rax

    # 1. ler paleta (hexadecimal %x)
    XOR r12, r12
.ler_paleta:
    CMP r12, 16
    JGE .fim_paleta
    LER_TEMP_DO_INPUT fmt_hex
    CMP eax, 1
    JNE .fim_programa
    MOV al, BYTE PTR [rip + temp_val]
    LEA r8, [rip + palette]
    MOV BYTE PTR [r8 + r12], al
    INC r12
    JMP .ler_paleta
.fim_paleta:

    # 2. ler quantidade de imagens (decimal %d)
    LER_TEMP_DO_INPUT fmt_dec
    CMP eax, 1
    JNE .fim_programa
    MOV eax, DWORD PTR [rip + temp_val]
    MOV DWORD PTR [rip + qtd_imagens], eax

.loop_imagens:
    MOV eax, DWORD PTR [rip + qtd_imagens]
    CMP eax, 0
    JLE .fim_programa

    # le a quant de triplas 'm' (decimal %d)
    LER_TEMP_DO_INPUT fmt_dec
    CMP eax, 1
    JNE .fim_programa
    
    MOV eax, DWORD PTR [rip + temp_val]
    MOV DWORD PTR [rip + expected_m], eax    

    # reseta as variaveis para a nova imagem
    MOV r12d, -1                    # max_x = -1
    MOV r13d, -1                    # max_y = -1
    MOV DWORD PTR [rip + max_b_val], 0 
    XOR r14, r14                    # contador de triplas = 0
    XOR rbx, rbx                    # bytes lidos = 0

    # le as triplas e pega o maior par de (y, x)
.ler_triplas:
    MOV eax, DWORD PTR [rip + expected_m]
    CMP r14d, eax
    JGE .processar

    # --- ler x, y e b (hexadecimal %x) e salvar direto nas triplas ---
    LER_E_SALVAR_TRIPLA fmt_hex, triplas_x, r14, .processar
    LER_E_SALVAR_TRIPLA fmt_hex, triplas_y, r14, .processar
    LER_E_SALVAR_TRIPLA fmt_hex, triplas_b, r14, .processar


    MOV r9d, eax                      # r9d = b lido
    LEA r8, [rip + triplas_y]
    MOV r11d, DWORD PTR [r8 + r14*4]  # r11d = y lido
    LEA r8, [rip + triplas_x]
    MOV r10d, DWORD PTR [r8 + r14*4]  # r10d = x lido

    # --- logica do maior par (y, x) ---
    CMP r11d, r13d                  # compara y atual com max_y
    JG .atualiza_maior_par          # se y atual for maior, atualiza tudo
    JL .pula_atualizacao            # se y atual for menor, ignora e avanca
    
    # se houver empate no y, o x decide
    CMP r10d, r12d                  # compara x atual com max_x
    JLE .pula_atualizacao           # se x for menor ou igual, ignora e avanca

.atualiza_maior_par:
    MOV r13d, r11d                  # atualiza max_y com o y atual
    MOV r12d, r10d                  # atualiza max_x com o x atual
    MOV DWORD PTR [rip + max_b_val], r9d # guarda o b deste par

.pula_atualizacao:
    ADD rbx, 3                      # conta 3 bytes para a compressao
    INC r14d
    JMP .ler_triplas


.processar:
    # --- calculo final de largura e altura ---
    MOV eax, DWORD PTR [rip + max_b_val]
    SHR eax, 4                      # extrai as repeticoes do maior par
    ADD r12d, eax                   # largura = max_x + repeticoes

    INC r13d                        # altura = max_y + 1

    MOV eax, r12d
    IMUL eax, r13d                  # calcula lxA  em eax
    MOV rdx, rax                    # rdx recebe o tamanho q tem q limpar
    LEA rdi, [rip + canvas]
    XOR esi, esi                    # preenche com 0 (\0)
    CALL memset@plt

    XOR r15d, r15d                  
.desenhar:
    MOV eax, DWORD PTR [rip + expected_m]
    CMP r15d, eax
    JGE .imprimir

    LEA r8, [rip + triplas_x]
    MOV eax, DWORD PTR [r8 + r15*4] # x
    
    LEA r8, [rip + triplas_y]
    MOV ecx, DWORD PTR [r8 + r15*4] # y
    
    LEA r8, [rip + triplas_b]
    MOV edx, DWORD PTR [r8 + r15*4] # b

    MOV edi, edx
    SHR edi, 4                      # extrai repeticoes
    AND edx, 0x0F                   # extrai indice
    
    LEA r8, [rip + palette]
    MOVZX edx, BYTE PTR [r8 + rdx]  # transforma indice no caractere

    # calcula a posicao na memoria = vetor[y][x] -> (y * largura) + x
    MOV r9d, eax                    
    MOV eax, ecx
    IMUL eax, r12d
    ADD eax, r9d                    
    
    LEA r8, [rip + canvas]
    LEA rcx, [r8 + rax]             

.rep_loop:
    TEST edi, edi
    JZ .next_triple
    MOV BYTE PTR [rcx], dl          # sobrescreve o caractere
    INC rcx                         # avanca o cursor 1 pixel (x + 1)
    DEC edi
    JMP .rep_loop

.next_triple:
    INC r15d
    JMP .desenhar

    # 6. matematica e saida de dados (percorrendo a matriz [y][x]
.imprimir:
    # area = largura * altura
    MOV eax, r12d
    IMUL eax, r13d
    MOV r9d, eax                    

    # taxa de compressao = (bytes lidos * 100) / area
    MOV rax, rbx                    
    IMUL rax, 100
    SHL rax, 1                    
    MOV rcx, r9                     
    TEST rcx, rcx                   
    JZ .taxa_zero
    XOR rdx, rdx                    
    DIV rcx
    ADD rax, 1 
    SHR rax, 1

    JMP .print_taxa

.taxa_zero:
    XOR rax, rax                    

.print_taxa:
    MOV rdi, QWORD PTR [rip + outputF]  
    LEA rsi, [rip + fmt_comp_rate]      
    MOV rax, rbx                        
    IMUL rax, 10000                     
    MOV rcx, r9                         
    TEST rcx, rcx                       
    JZ .taxa_zero_print                 
    MOV r11, rcx                        
    SHR r11, 1                          
    ADD rax, r11                        
    XOR rdx, rdx                        
    DIV rcx                             
    JMP .taxa_split                     

.taxa_zero_print:
    XOR rax, rax                        

.taxa_split:
    MOV rcx, 100                        
    XOR rdx, rdx                        
    DIV rcx                             
    MOV r10, rax                        
    MOV r11, rdx                        
    MOV rdx, r10                        
    MOV rcx, r11                        
    XOR eax, eax                    
    CALL fprintf@plt

    XOR r14d, r14d                  # y = 0
.print_y:
    CMP r14d, r13d
    JGE .fim_imagem
    
    XOR r15d, r15d                  # x = 0
.print_x:
    CMP r15d, r12d
    JGE .print_nl
    
    # acesso a matriz: vetor[y][x] -> [y * largura + x]
    MOV eax, r14d
    IMUL eax, r12d
    ADD eax, r15d
    
    LEA r8, [rip + canvas]
    MOVZX edx, BYTE PTR [r8 + rax]      
    
    MOV rdi, QWORD PTR [rip + outputF]
    LEA rsi, [rip + fmt_char]
    XOR eax, eax
    CALL fprintf@plt
    
    INC r15d
    JMP .print_x
    
.print_nl:
    MOV rdi, QWORD PTR [rip + outputF]
    LEA rsi, [rip + fmt_newline]
    XOR eax, eax
    CALL fprintf@plt
    
    INC r14d
    JMP .print_y

.fim_imagem:
    # subtrai 1 a qtd de imagens e recomeça o ciclo
    MOV eax, DWORD PTR [rip + qtd_imagens]
    DEC eax
    MOV DWORD PTR [rip + qtd_imagens], eax
    JMP .loop_imagens

    # epilogo

.fechar_input_e_sair:
    MOV rdi, QWORD PTR [rip + inputF]
    CALL fclose@plt
    JMP .fim_programa_sem_fechar

.fim_programa:
    MOV rdi, QWORD PTR [rip + inputF]
    CALL fclose@plt
    MOV rdi, QWORD PTR [rip + outputF]
    CALL fclose@plt

.fim_programa_sem_fechar:
    ADD rsp, 8
    POP r15
    POP r14
    POP r13
    POP r12
    POP rbx
    POP rbp
    MOV eax, 0
    RET
