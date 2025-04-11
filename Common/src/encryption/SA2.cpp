#include <stdio.h>
#include <stdint.h>
#define _BYTE  char

int main2(int argc, char **argv){
//int SA2[] = { 0x68, 0x03, 0x81, 0x93, 0x54, 0x41, 0x4A, 0x49, 0x4A, 0x05, 0x87, 0x53, 0x41, 0x4A, 0x49, 0x49, 0x4C };
// test OK (09D927750JE_1895.sgo)
// 06 06 06 07 ===> e3 76 c3 c5;   e9 29 ea 29 ===> 7e d3 f7 f4; 0a 0b 0c 0d ===> 10 e2 9e 9c; ab cd ef 00 ===> ac 38 80 06

//int SA2[] = { 0x68, 0x05, 0x81, 0x4A, 0x05, 0x87, 0x5F, 0xBD, 0x5D, 0xBD, 0x49, 0x4C}; // basano
// test OK (03C906056AE_8848.sgo)
// 0C F4 23 3D ===> C1 39 3A 1C;   10 66 74 01 ===> B3 B4 3B 58;

//int SA2[] = { 0x93, 0x00, 0x00, 0x37, 0x19, 0x4C };
// test OK (1Z0035161G_DE2_0033.sgo)
// fa f0 fe 11 ===> fa f1 35 2a;   06 06 06 07 ===> 06 06 3d 20;

//int SA2[] = { 0x68, 0x03, 0x87, 0x00, 0x3F, 0x17, 0x35, 0x93, 0xA3, 0xFF, 0x78, 0x90, 0x4A, 0x01, 0x82, 0x49, 0x4C };
// test OK (v074N0771K0___Lenkung_Reibung.sgo)
// 06 06 06 07 ===> 25 9e 0a c3;   fa f0 fe 11 ===> 4b b3 57 ed

//int SA2[] =  { 0x68, 0x02, 0x81, 0x49, 0x93, 0xa5, 0x5a, 0x55, 0xaa, 0x4a, 0x05, 0x87, 0x81, 0x05, 0x95, 0x26, 0x68, 0x05, 0x82, 0x49, 0x84, 0x5a, 0xa5, 0xaa, 0x55, 0x87, 0x03, 0xf7, 0x80, 0x6a, 0x4c };
// test OK (v0691c1202ea__getriebe_DSG_cT1c_sw.sgo)
// 01 02 03 04 ===> 79 52 e8 d2;   0a 0b 0c 0d ===> 98 31 09 b3;  aa bb cc dd ===> d8 13 36 e1;  ab cd ef 00 ===>  ff f0 91 a5;  11 22 33 44 ===>  7f 5e ee aa;  1a 1b 1c 1d ===>  6a 37 f0 2e;   2a 2b 2c 2d ===>  68 25 ea 2c

//int SA2[] =  { 0x68, 0x05, 0x93, 0x02, 0xe1, 0x42, 0x3a, 0x81, 0x81, 0x87, 0x34, 0x61, 0x12, 0x00, 0x81, 0x4a, 0x06, 0x82, 0x87, 0x06, 0xa3, 0xf2, 0x03, 0x87, 0x0a, 0x45, 0x64, 0x28, 0x49, 0x4c};
// test OK (03C906014CL_1098.sgo, 420910156C__0130.sgo, ...)
// 83 0c 8f 83 ===> fd 77 48 f4;   06 06 06 07 ===> bb 78 d0 98;  e9 29 ea 29 ===> 2f 70 cd 8c; 06 06 06 07 ===> bb 78 d0 98

//int SA2[] =  { 0x68, 0x04, 0x81, 0x93, 0x7f, 0x38, 0xe7, 0x5c, 0x4a, 0x07, 0x87, 0x29, 0x7c, 0x13, 0xa5, 0x6b, 0x05, 0x93, 0x3f, 0x9b, 0x2e, 0x4b, 0x49, 0x4c };
// test OK (1k0035180af_de2_0008.sgo)
// 01 02 03 04 ===> e2 d0 75 6d;   0a 0b 0c 0d ===> 07 da 4b 36;  aa bb cc dd ===> 67 5c 7d 2d; 2a 2b 2c 2d ===> 68 5a 8a d5

int SA2[] =  { 0x68, 0x03, 0x81, 0x93, 0x54, 0x41, 0x4A, 0x49, 0x4A, 0x05, 0x87, 0x53, 0x41, 0x4A, 0x49, 0x49, 0x4C };

	int DEBUG_LEVEL = 0;

    if( argc <= 1) {
      printf("USAGE: %s 0xSEEEEEED\n", argv[0]);
      return 0;
    }
   
    unsigned int v11; //seed (i.e.: 0x0a0b0c0d or 0xe929ea29
    v11 = strtol(argv[1], NULL, 16);
	
	unsigned int v12; //rsl
	int v14; // rsr

	uint16_t opadd_value1; //add
	unsigned int v17; //add
	int v18; // add
	
	uint16_t opsub_value1; //sub
	unsigned int v20; //sub
	int v21; // sub
	
	uint16_t opxor_value1; //xor
    int v25; //xor
   
	int v34; // bcc position byte
	
	int v36; // bra position byte
	
	int v48 = 0; // C-flag
	
	int v3; // sa2 array position
	
	int v27; // loop
	int *v29; // loop
	char v32; // loop
	int v50; // next
	int *v51; // loop & next
	int v52;
	int *v53;  // loop & next

	v3 = 0;
	v52 = 0;

	v50 = -1;
    v53 = &v50;
    v51 = (int *)&v51;
   
	if(DEBUG_LEVEL >= 1)printf("START. SEED = %08lx ", v11);
	if(DEBUG_LEVEL >= 1)
	{
		 printf("[%%d DEC: %d  %%u DEC: %u]\n", v11, v11);
	 } else {
		 //printf("\n");
	}

while(1)
{
	  if( SA2[v3] == 0x81 )
	  {
		 if(DEBUG_LEVEL >= 1){ printf("=> RSL\n"); }
          v12 = v11 & 0x80000000;
          v11 <<= 1;
          if ( v12 )
          {
            v11 |= 1u;
            v48 = 1;
          }
          else
          {
            v48 = 0;
          }
          ++v3;
      }
      else if ( SA2[v3] == 0x82 )
      {
         if(DEBUG_LEVEL >= 1){ printf("=> RSR\n"); }
		  v14 = v11 & 1;
          v11 >>= 1;
          if ( v14 )
          {
            v11 |= 0x80000000;
            v48 = 1;
          }
          else
          {
            v48 = 0;
          }
          ++v3;
       }
	   else if ( SA2[v3] == 0x93 )
      {
	     if(DEBUG_LEVEL >= 1){ printf("=> ADD\n"); }
         opadd_value1 = (SA2[v3 + 1] << 8) | SA2[v3 + 2]; // concat 2 bytes
         v17 = v11;
         v18 = SA2[v3 + 4] | ((SA2[v3 + 3] | (opadd_value1 << 8)) << 8);
		 v11 = v11 + v18;
		 v48 = v11 < v17;
		 v3 += 5;
	   }
       else if ( SA2[v3] == 0x84 )
       {
	     if(DEBUG_LEVEL >= 1){ printf("=> SUB\n"); }

          opsub_value1 = (SA2[v3 + 1] << 8) | SA2[v3 + 2]; // concat 2 bytes
          v20 = SA2[v3 + 4] | ((SA2[v3 + 3] | (opsub_value1 << 8)) << 8);
          v21 = v11 < v20;
          v11 -= v20;
          v48 = v21;
          v3 += 5;
		}
		else if ( SA2[v3] == 0x87 )
		{
		 if(DEBUG_LEVEL >= 1){ printf("=> XOR\n"); }

         opxor_value1 = (SA2[v3 + 1] << 8) | SA2[v3 + 2]; // concat 2 bytes
         v25 = SA2[v3 + 4] | ((SA2[v3 + 3] | (opxor_value1 << 8)) << 8);

		 v11 = v11 ^ v25;
		 v3 += 5;
		 v48 = 0;
		}
		
		else if ( SA2[v3] == 0x68 )
		{
		 if(DEBUG_LEVEL >= 1){ printf("=> LOOP\n"); }
		 v27 = v3 + 1;
         v51 += 2;
         v29 = v53 + 2;

         v53 += 2;

         v32 = SA2[v27];
         v3 = v27 + 1;
         *(_BYTE *)v51 = v32;
         *v29 = v3;
        }
         
        else if ( SA2[v3] == 0x49 )
        {
	      if(DEBUG_LEVEL >= 1){ printf("=> NEXT\n"); }
          if ( *(_BYTE *)v51 <= 1u )
          {
            v51 -= 2;
            --v50;
            v53 -= 2;
            ++v3;
            if(DEBUG_LEVEL >= 2){ printf("NEXT 1: %X at pos %d\n", SA2[v3], v3); }
          }
          else
          {
            --*(_BYTE *)v51;
            v3 = *v53;
            if(DEBUG_LEVEL >= 2){ printf("NEXT 2: %X at pos %d\n", SA2[v3], v3); }
          }
		}
		
        else if ( SA2[v3] == 0x4A )
        {
		  if(DEBUG_LEVEL >= 1){ printf("=> BCC\n"); }
          v34 = v3 + 1;

          if ( v48 )
            v3 = v34 + 1;
          else
            v3 = v34 + SA2[v34] + 1;
            if(DEBUG_LEVEL >= 2){ printf("BCC: %X at pos %d\n", SA2[v3], v3); }
        }

        else if ( SA2[v3] == 0x6B )
        {
          if(DEBUG_LEVEL >= 1){ printf("=> BRA\n"); }
          v36 = v3 + 1;
          v3 = v36 + SA2[v36] + 1;
        }

		else if ( SA2[v3] == 0x4C )
		{
		 if(DEBUG_LEVEL >= 1){ printf("=> READY\n"); }
		 printf("DONE. KEY = %08lx \n", v11);
		 return 0;
		}
		
	    else {
	     printf("ERROR: ILLEGAL OPCODE %X at pos %d\n", SA2[v3], v3);
		 return 0;
		}
	if(DEBUG_LEVEL >= 1)printf("CURRENT SEED = %08lx \n", v11);
}

printf("DONE. KEY = %08lx \n", v11);
}
