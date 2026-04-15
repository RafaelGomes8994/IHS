.intel_syntax noprefix

.section .note.GNU-stack, "", @progbits

.section .data
    fmt_hex:        .asciz "%x"             # Para Paleta, X, Y e B
    fmt_dec:        .asciz "%d"             # Apenas para Qtd Imagens e M
    fmt_char:       .asciz "%c"
    fmt_newline:    .asciz "\n"
    fmt_comp_rate:  .asciz "[%d%%]\n"

    mode_r:         .asciz "r"              
    mode_w:         .asciz "w"              

.section .bss
    .lcomm palette, 16              
    
    .lcomm canvas, 4000000          # 4 Megabytes de Matriz 2D!
    .lcomm triplas_x, 4000000       # Posições suficientes para ficheiros enormes
    .lcomm triplas_y, 4000000         
    .lcomm triplas_b, 4000000         
    
    .lcomm temp_val, 4
    .lcomm max_b_val, 4             # Guarda o 'B' do maior par (Y, X)
    .lcomm qtd_imagens, 4
    .lcomm expected_m, 4            

    .lcomm inputF, 8                
    .lcomm outputF, 8               

.section .text
.global main

main:
    # PRÓLOGO E ABRIR FICHEIROS
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

    # 1. LER PALETA (Hexadecimal %x)
    XOR r12, r12
.ler_paleta:
    CMP r12, 16
    JGE .fim_paleta
    MOV rdi, QWORD PTR [rip + inputF]   
    LEA rsi, [rip + fmt_hex]            
    LEA rdx, [rip + temp_val]           
    XOR eax, eax                        
    CALL fscanf@plt
    CMP eax, 1
    JNE .fim_programa
    MOV al, BYTE PTR [rip + temp_val]
    LEA r8, [rip + palette]
    MOV BYTE PTR [r8 + r12], al
    INC r12
    JMP .ler_paleta
.fim_paleta:

    # 2. LER QUANTIDADE DE IMAGENS (Decimal %d)
    MOV rdi, QWORD PTR [rip + inputF]
    LEA rsi, [rip + fmt_dec]            
    LEA rdx, [rip + temp_val]
    XOR eax, eax
    CALL fscanf@plt
    CMP eax, 1
    JNE .fim_programa
    MOV eax, DWORD PTR [rip + temp_val]
    MOV DWORD PTR [rip + qtd_imagens], eax

.loop_imagens:
    MOV eax, DWORD PTR [rip + qtd_imagens]
    CMP eax, 0
    JLE .fim_programa

    # 3. LER QUANTIDADE DE TRIPLAS 'm' (Decimal %d)
    MOV rdi, QWORD PTR [rip + inputF]
    LEA rsi, [rip + fmt_dec]            
    LEA rdx, [rip + temp_val]
    XOR eax, eax
    CALL fscanf@plt
    CMP eax, 1
    JNE .fim_programa
    
    MOV eax, DWORD PTR [rip + temp_val]
    MOV DWORD PTR [rip + expected_m], eax    

    # Reset das variáveis para a nova imagem
    MOV r12d, -1                    # max_x = -1
    MOV r13d, -1                    # max_y = -1
    MOV DWORD PTR [rip + max_b_val], 0 
    XOR r14, r14                    # contador de triplas = 0
    XOR rbx, rbx                    # bytes lidos = 0

    # 4. LER AS TRIPLAS E DESCOBRIR O MAIOR PAR (Y, X) 
.ler_triplas:
    MOV eax, DWORD PTR [rip + expected_m]
    CMP r14d, eax
    JGE .processar

    # --- LER X (Hexadecimal %x) ---
    MOV rdi, QWORD PTR [rip + inputF]
    LEA rsi, [rip + fmt_hex]
    LEA rdx, [rip + temp_val]
    XOR eax, eax
    CALL fscanf@plt
    CMP eax, 1
    JNE .processar
    
    MOV eax, DWORD PTR [rip + temp_val]
    LEA r8, [rip + triplas_x]       
    MOV DWORD PTR [r8 + r14*4], eax

.ler_y:
    # --- LER Y (Hexadecimal %x) ---
    MOV rdi, QWORD PTR [rip + inputF]
    LEA rsi, [rip + fmt_hex]
    LEA rdx, [rip + temp_val]
    XOR eax, eax
    CALL fscanf@plt
    CMP eax, 1
    JNE .processar
    
    MOV eax, DWORD PTR [rip + temp_val]
    LEA r8, [rip + triplas_y]
    MOV DWORD PTR [r8 + r14*4], eax

.ler_b:
    # --- LER B (Hexadecimal %x) ---
    MOV rdi, QWORD PTR [rip + inputF]
    LEA rsi, [rip + fmt_hex]
    LEA rdx, [rip + temp_val]
    XOR eax, eax
    CALL fscanf@plt
    CMP eax, 1
    JNE .processar
    
    MOV eax, DWORD PTR [rip + temp_val]
    LEA r8, [rip + triplas_b]
    MOV DWORD PTR [r8 + r14*4], eax


    MOV r9d, eax                      # r9d = B (Acabou de ser lido)
    LEA r8, [rip + triplas_y]
    MOV r11d, DWORD PTR [r8 + r14*4]  # r11d = Y intacto!
    LEA r8, [rip + triplas_x]
    MOV r10d, DWORD PTR [r8 + r14*4]  # r10d = X intacto!

    # --- LÓGICA DO MAIOR PAR (Y, X) ---
    CMP r11d, r13d                  # Compara Y atual com max_y
    JG .atualiza_maior_par          # Se Y atual for MAIOR, atualiza tudo!
    JL .pula_atualizacao            # Se Y atual for MENOR, ignora e avança
    
    # Se houver empate no Y (Y atual == max_y), o X decide
    CMP r10d, r12d                  # Compara X atual com max_x
    JLE .pula_atualizacao           # Se X for MENOR ou IGUAL, ignora e avança

.atualiza_maior_par:
    MOV r13d, r11d                  # Atualiza max_y com o Y atual
    MOV r12d, r10d                  # Atualiza max_x com o X atual
    MOV DWORD PTR [rip + max_b_val], r9d # Guarda o B pertencente a este par!

.pula_atualizacao:
    ADD rbx, 3                      # Conta 3 bytes para a compressão
    INC r14d
    JMP .ler_triplas

    # 5. PREPARAR A MATRIZ 2D E DESENHAR

.processar:
    # --- CÁLCULO FINAL DE LARGURA E ALTURA ---
    MOV eax, DWORD PTR [rip + max_b_val]
    SHR eax, 4                      # Extrai as Repetições do maior par
    ADD r12d, eax                   # LARGURA = max_x + Repetições

    INC r13d                        # ALTURA = max_y + 1

    # MEMSET CIRÚRGICO: Limpa apenas a Área LxA com ZERO ABSOLUTO (\0)
    MOV eax, r12d
    IMUL eax, r13d                  # Calcula LxA exato em eax
    MOV rdx, rax                    # RDX recebe o tamanho exato a limpar
    LEA rdi, [rip + canvas]
    XOR esi, esi                    # Preenche com 0 (\0)
    CALL memset@plt

    XOR r15d, r15d                  
.desenhar:
    MOV eax, DWORD PTR [rip + expected_m]
    CMP r15d, eax
    JGE .imprimir

    LEA r8, [rip + triplas_x]
    MOV eax, DWORD PTR [r8 + r15*4] # X
    
    LEA r8, [rip + triplas_y]
    MOV ecx, DWORD PTR [r8 + r15*4] # Y
    
    LEA r8, [rip + triplas_b]
    MOV edx, DWORD PTR [r8 + r15*4] # B

    MOV edi, edx
    SHR edi, 4                      # Extrai Repetições
    AND edx, 0x0F                   # Extrai Índice
    
    LEA r8, [rip + palette]
    MOVZX edx, BYTE PTR [r8 + rdx]  # Transforma Índice no Caractere

    # Calcula a Posição na Memória = vetor[Y][X] -> (Y * Largura) + X
    MOV r9d, eax                    
    MOV eax, ecx
    IMUL eax, r12d
    ADD eax, r9d                    
    
    LEA r8, [rip + canvas]
    LEA rcx, [r8 + rax]             

.rep_loop:
    TEST edi, edi
    JZ .next_triple
    MOV BYTE PTR [rcx], dl          # Subscreve o caractere perfeitamente
    INC rcx                         # Avança o cursor 1 pixel (X + 1)
    DEC edi
    JMP .rep_loop

.next_triple:
    INC r15d
    JMP .desenhar

    # 6. MATEMÁTICA E SAÍDA DE DADOS (Percorrendo a Matriz [Y][X]
.imprimir:
    # Área = Largura * Altura
    MOV eax, r12d
    IMUL eax, r13d
    MOV r9d, eax                    

    # Taxa de Compressão = (Bytes Lidos * 100) / Área
    MOV rax, rbx                    
    IMUL rax, 100                   
    
    MOV rcx, r9                     
    TEST rcx, rcx                   
    JZ .taxa_zero
    XOR rdx, rdx                    
    DIV rcx                         
    JMP .print_taxa

.taxa_zero:
    XOR rax, rax                    

.print_taxa:
    MOV rdi, QWORD PTR [rip + outputF]  
    LEA rsi, [rip + fmt_comp_rate]      
    MOV rdx, rax                        
    XOR eax, eax                    
    CALL fprintf@plt

    XOR r14d, r14d                  # Y = 0
.print_y:
    CMP r14d, r13d
    JGE .fim_imagem
    
    XOR r15d, r15d                  # X = 0
.print_x:
    CMP r15d, r12d
    JGE .print_nl
    
    # Acesso à Matriz: vetor[Y][X] -> [Y * Largura + X]
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
    MOV rdi, QWORD PTR [rip + outputF]
    LEA rsi, [rip + fmt_newline]
    XOR eax, eax
    CALL fprintf@plt

    # Subtrai 1 à Qtd de Imagens e recomeça o ciclo
    MOV eax, DWORD PTR [rip + qtd_imagens]
    DEC eax
    MOV DWORD PTR [rip + qtd_imagens], eax
    JMP .loop_imagens

    # EPÍLOGO 

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

