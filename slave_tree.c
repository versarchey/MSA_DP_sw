//从核只负责打分矩阵的计算
//两个概念：
//打分矩阵元：一个64*64的打分矩阵部分
//打分矩阵元组：由1~64个打分矩阵元所构成的写对角线
#include <stdio.h>
#include <math.h>
#include <string.h>
#include<time.h>
#include<sys/types.h>
#include<sys/stat.h>
#include "slave.h"

#define I 30000			//I为比对序列的最大长度
#define S 100					//S为比对序列的最大条数
#define d -5			//d为空位罚分, 三个预设参数之一
#define max_line 1024	//规定读取文件时每次读取的一行中最大的字符数

//!!!定义局存变量
__thread_local volatile unsigned long get_reply, put_reply;	//定义并初始化传输回答字
__thread_local volatile unsigned long put_reply2[2];		//双缓冲模式
__thread_local volatile unsigned long start,end;	
__thread_local int my_id;									//从核的编号
__thread_local char b1[125],b2[125];						//承接本轮要比对的打分矩阵元所对应a1和a2中的序列部分
__thread_local int matrix_slave[124][124];					//定义在本从核周期中需要计算的打分矩阵元的暂存空间
__thread_local volatile int start_slave,loop_slave;			//承接主存中的start_num和loop
__thread_local volatile int row_slave,col_slave;						//承接主存中的row和col
__thread_local int mat_s,mis_s;								//承接主存中的match和mis_match
__thread_local int matrix_firstup[125];						//承接本次要计算的打分矩阵元中的第一行
__thread_local int matrix_firstleft[124];					//承接本次要计算的打分矩阵元中的第一列


//!!!外部引入主存中的变量
extern volatile int matrix[I][I];	//matrix为打分矩阵
extern char an[S][I];				//an用于存储至多100条需要进行比对的序列
extern volatile int DM[S][S];				//DM为距离矩阵
extern int seq_num;					//seq_num为an中实际储存的序列数
extern volatile int row,col;
extern int match,mis_match;	//row和col用于记录a1/2两条比对序列的长度, 并将其作为打分矩阵实际的行数与列数（打分矩阵的实际大小为(col+1)*(row+1)）, match和mis_match为比对的预设参数, 再text.txt中被指定
extern volatile int start_num,loop;	//start_num和loop用于指示当前计算的轮次, 其中start_num指示当前计算的打分矩阵元组所在的行轮次数， 其中loop指示当前计算的打分矩阵元组所在的列轮次数
extern volatile char a1[I],a2[I];			//a1/2用于存储需要进行比对的两条序列


//!!!从核函数
void func()
{	
	get_reply=0;
    put_reply=0;
	my_id=athread_get_id(-1);		//获取从核编号
	athread_get(PE_MODE,&start_num,&start_slave,4,&get_reply,0,0,0);	//获取从核周期的行号
	athread_get(PE_MODE,&loop,&loop_slave,4,&get_reply,0,0,0);			//获取从核周期的列号
	athread_get(PE_MODE,&row,&row_slave,4,&get_reply,0,0,0);			//获取打分矩阵的行数
	athread_get(PE_MODE,&col,&col_slave,4,&get_reply,0,0,0);			//获取打分矩阵的列数
	athread_get(PE_MODE,&match,&mat_s,4,&get_reply,0,0,0);				//获取匹配得分
	athread_get(PE_MODE,&mis_match,&mis_s,4,&get_reply,0,0,0);			//获取未匹配得分
	while(get_reply!=6);			//等待DMA传输完成
	if(my_id<loop_slave)			//根据打分矩阵元组的loop编号判断所含打分矩阵元的数量，进而判断要启动的从核个数
	{
		int b1_start=(start_slave-1)*(124*64)+my_id*124;
		int b2_start=(loop_slave-my_id-1)*124;				//判断每一个从核所计算的打分矩阵元所对应的a1/2中的序列部分的起始位置
		get_reply=0;
		athread_get(PE_MODE,&a1[b1_start],&b1[0],124,&get_reply,0,0,0);	//获取打分矩阵元所对应的a1中的序列部分
		athread_get(PE_MODE,&a2[b2_start],&b2[0],124,&get_reply,0,0,0);	//获取打分矩阵元所对应的a2中的序列部分
		while(get_reply!=2);		//等待DMA传输完成
		b1[124]='\0';
		b2[124]='\0';
		int len1=strlen(a1);	//将长度复制进变量中，防止重复计算
		int len2=strlen(a2);

		int i,j;
		get_reply=0;
		athread_get(PE_MODE,&matrix[b1_start][b2_start],&matrix_firstup[0],125*4,&get_reply,0,0,0);	//从打分矩阵中取出该打分矩阵元所对应的第一行（改行在之前已被计算得到）
		while(get_reply!=1);
		int o;
		for(o=1;o<125;o++)		//从打分矩阵中取出该打分矩阵元所对应的第一列（改列在之前已被计算得到）
		{		
			get_reply=0;
			athread_get(PE_MODE,&matrix[b1_start+o][b2_start],&matrix_firstleft[o-1],4,&get_reply,0,0,0);
			while(get_reply!=1);	//等待DMA传输完成
		}
		for(i=0;i<124;i++)	//开始计算打分矩阵元，i为行
		{
			if(b1_start+i >= row_slave)				//如果当前计算的元素的行坐标已超出最大行数，则循环结束
			{
				break;
			}
			else
			{
				int index,last;		//双缓冲模式的控制变量
				index = i%2;
				last = (i-1)%2;
				for(j=0; j<124; j++)	//j为列
				{
					int t1,t2,t3,t3_score,t_max;	//t3_score为比对的分值（匹配为5，未匹配为-4），t1/2/3分别表示由上/左/对角方向得到的值，tmax为t1/2/3中的最大值
					if(b2_start+j >= col_slave)		//如果当前计算的元素的列坐标已超出最大列数，则破除内循环
					{
							break;
					}
					else
					{
						if(b1[i] == b2[j])
						{
							t3_score = mat_s;
						}
						else
						{
							t3_score = mis_s;
						}
						if(i == 0)		//第一行/列的计算需要依赖之前计算好的打分矩阵（储存在matrix_firstup和matrix_firstleft中）
						{
							if(j == 0)	//第一行/列的计算需要依赖之前计算好的打分矩阵（储存在matrix_firstup和matrix_firstleft中）
							{
								t3=matrix_firstup[0]+t3_score;
								t2=matrix_firstleft[i]+d;
							}
							else
							{
								t2=matrix_slave[i][j-1]+d;
								t3=matrix_firstup[j]+t3_score;
							}
							t1=matrix_firstup[j+1]+d;
						}
						else			
						{
							if(j==0)	//第一行/列的计算需要依赖之前计算好的打分矩阵（储存在matrix_firstup和matrix_firstleft中）
							{
								t2=matrix_firstleft[i]+d;
								t3=matrix_firstleft[i-1]+t3_score;
							}
							else
							{
								t2=matrix_slave[i][j-1]+d;
								t3=matrix_slave[i-1][j-1]+t3_score;
							}
							t1=matrix_slave[i-1][j]+d;
						}
						t_max=t1;		//比较t1/2/3
						if(t2>t1)
						{
							t_max=t2;	//比较t1/2/3
						}
						if(t3>t_max)	//比较t1/2/3
						{
							t_max=t3;
						}
						matrix_slave[i][j]=t_max;	//打分矩阵元中第(i,j)的元素计算完成
					}
					 
					/////////////打印本次比对的参数
					if(len2==2772 && len1==1865 && loop_slave==1 && i==1 && j==2)
					{
						printf("s1:%c; s2:%c\nt_max:%d\n", b1[i], b2[i], t_max);
						printf("t3_score:%d\n",t3_score);
						printf("b1:%s\nb2:%s\n",b1,b2);
					}
					/////////////

				}
				
				// if(j!=0)	//不采用双缓冲模式的数据放送
				// {
				// 	put_reply=0;
				// 	athread_put(PE_MODE,&matrix_slave[i][0],&matrix[b1_start+i+1][b2_start+1],j*4,&put_reply,0,0);//将计算得到的当前元素值put到主存
				// 	while (put_reply!=1);
				// }
				if(j!=0)
				{
					put_reply2[index]=0;
					athread_put(PE_MODE,&matrix_slave[i][0],&matrix[b1_start+i+1][b2_start+1],j*4,&put_reply2[index],0,0);//将计算得到的当前元素值put到主存while(put_reply!=1);
					if(i>0){
						while(put_reply2[last]!=1);
					}
					if(b1_start+i+1>=row_slave)
					{
						while(put_reply2[index]!=1);
					}
				}
			}
		}
		/////////打印一次计算单位矩阵
		// if(len2==2772 && len1==1865 && loop_slave==1)
		// {
		// 	FILE *fq=NULL;
		// 	fq=fopen("result.txt","w+");
		// 	for(i=0;i<124;i++)
		// 	{
		// 		for(j=0;j<124;j++)
		// 		{
		// 			fprintf(fq,"%d\t",matrix_slave[i][j]);
		// 		}
		// 		fprintf(fq,"\n");
		// 	}
		// 	fclose(fq);
		// }
		/////////
	}
}
