//0号进程负责文件的读、写以及回溯，所有进程共同负责打分矩阵的计算
//主核负责IO、文件的读写、储存空间的开辟、打分矩阵的回溯以及打分矩阵第1行/列的计算
//概念：
//打分矩阵元：一个124*124的打分矩阵部分
//打分矩阵元组：由1~64个打分矩阵元组成的元组，一个元组会在一个计算周期被一个进程计算完毕
//打分矩阵超元组：由1~64*P个打分矩阵元组成，一个超元组会在一个计算周期被所有进程共同计算完毕
#include<string.h>
#include<stdio.h>
#include<stdlib.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<athread.h>
#include<fcntl.h>
#include<time.h>
//#include <unistd.h> 
#include<mpi.h>
//#include<swlu.h>

//124本也该定义为宏
#define I 30000					//I为比对序列的最大长度
#define S 100					//S为比对序列的最大条数
#define d -5					//d为空位罚分, 三个预设参数之一
#define max_line 1024			//规定读取文件时每次读取的一行中最大的字符数

volatile int matrix[I][I];		//matrix为打分矩阵
char an[S][I];				//an用于存储至多100条需要进行比对的序列
volatile int DM[S][S];				//DM为距离矩阵
int seq_num;					//seq_num为an中实际储存的序列数
volatile int row,col;
int match,mis_match;	//row和col用于记录a1/2两条比对序列的长度, 并将其作为打分矩阵实际的行数与列数（打分矩阵的实际大小为(col+1)*(row+1)）, match和mis_match为比对的预设参数, 再text.txt中被指定
volatile int start_num,loop;	//start_num和loop用于指示当前计算的轮次, 其中start_num指示当前计算的打分矩阵元组所在的行轮次数， 其中loop指示当前计算的打分矩阵元组所在的列轮次数
volatile char a1[I],a2[I];				//a1/2用于存储需要进行比对的两条序列



extern SLAVE_FUN(func)();

static void fun(char *str)		//序列倒置，用于将比对结果序列的顺序纠正
{
	int len=strlen(str);
	char temp;
        int i;
	for(i=0;i<len/2;i++)
	{
		temp=str[i];
		str[i]=str[len-1-i];
		str[len-1-i]=temp;
	}
}

static inline unsigned long rpcc()	//节拍计数器
{
	unsigned long time;
	asm("rtc %0":"=r"(time):);
	return time;
}

int main(int argc, char** argv)
{
	int mpi_num, mpi_id;			//mpi_num为进程数，mpi_id为当前进程的编号
    MPI_Status status;
	MPI_Init(&argc, &argv);			//mpi进程初始化
    MPI_Comm_size(MPI_COMM_WORLD, &mpi_num);
    MPI_Comm_rank(MPI_COMM_WORLD, &mpi_id);
	if(1)
	{
///////////////计时变量///////////////
		clock_t time_start1,time_over1;	//计时1包含了打分矩阵的计算与回溯以及文件的读写的耗时
		clock_t time_start2,time_over2;	//计时2包含了打分矩阵的计算与回溯的耗时
		clock_t time_start3,time_over3;	//计时3只包含了打分矩阵的计算部分用时
		unsigned long st1,ed1,st2,ed2;
		st1=rpcc();
		double run_time,run_time1,run_time2,run_time3;
///////////////计时变量///////////////

		time_start1=clock();			//计时1开始

/////////////////////////////////////!!!!读入文件开始!!!!////////////////////////////////////
		if(mpi_id == 0)					//由0号进程读取文件，然后广播到所有进程
		{
			FILE *file;
			char buf[max_line];							//用于接受并暂存读取文件的buffer
			int p1,p2,i=1;								//i表示当前读取的行号, p1/2用于暂存从第一二行转换出来的预设参数
			file=fopen("text_all.txt","r");					//打开text_all.txt文件
			int name=0;								//name用于指示当前读取的字符串属于s1/2，默认为s1
			if(!file)
			{
				printf("can't open file\n");
			}
			else
            {
				while(fgets(buf,max_line,file)!=NULL)	//读取text.txt中信息，每次读取一行
				{
					if(i==1)							//从第一行读取预设参数match
					{
						sscanf(buf,"%d",&p1);	
						match=p1;
						i++;
					}
					else if(i==2)						//从第二行读取预设参数mis_match
					{
						sscanf(buf,"%d",&p2);	
						mis_match=p2;
						i++;
					}
					else
                    {							
                        if(buf[strlen(buf)-2]==':')    //将name指向下一条序列
						{
							name++;
							continue; 
						}
						else							//如果不为链指示行, 则执行读取操作
						{	
							int len=strlen(buf);	
							if(buf[len-1]=='\n')		//如果本次读取的最后一个字符为换行符，则将换行符之前的内容作为有效的读取内容
							{
								buf[len-1]='\0';
							}
							strcat(an[name-1],buf);     //将buf合并到第name条序列上
						}
					}
				}
			}
			seq_num=name;
            //printf("name:%d\n",name);
			fclose(file);
		}
		MPI_Bcast(&match,1,MPI_INT,0,MPI_COMM_WORLD);	//0号进程向其他进程发送读取到的序列以及参数
		MPI_Bcast(&mis_match,1,MPI_INT,0,MPI_COMM_WORLD);
		MPI_Bcast(&seq_num,1,MPI_INT,0,MPI_COMM_WORLD);
		MPI_Bcast(an[0],I*seq_num,MPI_CHAR,0,MPI_COMM_WORLD);	
		MPI_Barrier(MPI_COMM_WORLD);

			// printf("kkkk\n");
/////////////////////////////////////!!!!读入文件结束!!!!////////////////////////////////////

		time_start2=clock();			//计时2开始
		st2=rpcc();
		time_start3=clock();		//计时3开始

/////////////////////////////////////!!!!打分矩阵计算开始!!!!////////////////////////////////////
		
		volatile int seq1_id, seq2_id, r_s=((float)seq_num/(float)mpi_num)*(float)mpi_id, r_e=((float)seq_num/(float)mpi_num)*((float)mpi_id+1)-1;	//r_s和r_e分别为当前进程所负责计算的距离矩阵行数的起点与终点，通过mpi_id来判断自己所需要计算的距离矩阵部分
		printf("%d~%d\n",r_s,r_e);
		if (r_e >= seq_num)		//最后一个进程的r_e可能会越界
		{
			r_e=seq_num;
		}
		athread_init();				//计算密集区, 正式开始计算打分矩阵, 多线程部分启动
		for(seq1_id=r_s; seq1_id<=r_e; seq1_id++)	//利用该循环完成所有序列的计算
		{
			for(seq2_id=0; seq2_id<seq_num; seq2_id++)	
			{
				if(strlen(an[seq1_id]) > strlen(an[seq2_id]))	//让更长的序列作为a2,这样提高并行效率
				{
					strcpy(&a2[0],&an[seq1_id][0]);
					strcpy(&a1[0],&an[seq2_id][0]);
				}
				else
				{
					strcpy(&a1[0],&an[seq1_id][0]);
					strcpy(&a2[0],&an[seq2_id][0]);
				}
				row=strlen(&a1[0]);				//将比对序列2的长度赋给row行数
				col=strlen(&a2[0]);				//将比对序列1的长度赋给col列数
				int o;
				for(o=0;o<=col;o++)			//计算打分矩阵的第1行的值
				{
					matrix[0][o]=o*d;
				}
				for(o=0;o<=row;o++)			//计算打分矩阵的第1列的值
				{
					matrix[o][0]=o*d;
				}
				int loop_r,loop_c;		//loop_c/r为打分矩阵列/行求解所需的从核周期数, 在一个从核周期内, 从核组可以计算完成一个打分矩阵元组的计算
				// if(col<=124*63)				//计算完成列计算所需要的从核周期数 (124也为预设变量, 124是根据从核局存的大小而设定好的)
				// {
				// 	loop_c=(col/124+1)*2-1;	//若列数<=124*63，则不足以发动所有的从核进行计算
				// }
				// else {
				// 	loop_c=(col/124+1)+63;	//若列数>124*63，则可以发动所有的从核进行计算
				// }							
				// loop_r=row/(124*64)+1;		//计算完成行计算所需要的从核周期数
				loop_c=col/124+1;
				loop_r=row/124+1;
				for(start_num=1;start_num<=loop_r;start_num++)	//根据之前计算的到loop_c/r, 将打分矩阵共分成loop_c*loop_r个打分矩阵元组, 每次计算一个打分矩阵元组
				{
					for(loop=1;loop<=loop_c;loop++)
					{
						// printf("%d",loop);	//打印列轮次
						athread_spawn(func,0);					
						// printf("!!!BEGIN SPAWN!!!!\n" );
						athread_join();
						// printf("!!!BEGIN JOIN!!!!!\n" );
					}
				}
				// printf("!!!END INIT!!!!!!\n" );

				DM[seq1_id][seq2_id]=matrix[row][col];	//将本次计算的全局比对得分赋予距离矩阵
				// if(seq1_id==5&&seq2_id==0)		//打印某次的得分矩阵
				// {
				// 	FILE *fq=NULL;
				// 	fq=fopen("result.txt","w+");
				// 	//fprintf(fq,"%s\n%s\n",a1,a2);
				// 	int is_r, is_c;
				// 	for(is_r=0;is_r<=row;is_r++)		
				// 	{
				// 		for(is_c=0;is_c<=col;is_c++)
				// 		{
				// 			fprintf(fq,"%d\t",matrix[is_r][is_c]);
				// 		}
				// 		fprintf(fq,"\n");
				// 	}
				// 	fclose(fq);
				// }

				//
				// int is_r, is_c;					//初始化打分矩阵
				// for(is_r=0;is_r<=row;is_r++)		
				// {
				// 	for(is_c=0;is_c<=col;is_c++)
				// 	{
				// 		matrix[is_r][is_c]=0;
				// 	}
				// }
			}
		}
		athread_halt();				//计算完成, 多线程部分终止
/////////////////////////////////////!!!!打分矩阵计算结束!!!!////////////////////////////////////

		time_over3=clock();			//计时3结束

/////////////////////////////////////!!!!距离矩阵的回收与输出!!!!////////////////////////////////////
		MPI_Barrier(MPI_COMM_WORLD);
		int is_id;
		for(is_id=0; is_id<mpi_num; is_id++)
		{
			if(mpi_id==is_id)
			{
				int is_r, is_c;
				for(is_r=r_s; is_r<=r_e; is_r++)
				{
					for(is_c=0; is_c<seq_num; is_c++)
					{
						printf("(%d,%d)%d\t",is_r,is_c,DM[is_r][is_c]);
					}
					printf("\n");
				}
			}
			MPI_Barrier(MPI_COMM_WORLD);
		}
		MPI_Barrier(MPI_COMM_WORLD);



/////////////////////////////////////!!!!回溯开始!!!!////////////////////////////////////
		// MPI_Barrier(MPI_COMM_WORLD);
		// char l1[I];					//l1为s1的比对结果序列
		// char l2[I];					//l2为s2的比对结果序列
		// int s1_l=0,s2_l=0,count=0;	//count为比对结果序列的长度，s1/2_l分别为l1/2的长度
		// if(1)					//0号进程负责回溯
		// {
		// 	int recall_x,recall_y;		//recall_x/y分别代表本轮回溯的行/列坐标
		// 	for(recall_x=row,recall_y=col;recall_x>0&&recall_y>0;)	//开始逆向回溯
		// 	{
		// 		int d_score;			//d_score为发生比对的得分，如果匹配成功则为match得分，如果匹配失败则为mis_match得分
		// 		if(a1[recall_y-1]==a2[recall_x-1])
		// 			d_score=match;		//match
		// 		else {
		// 			d_score=mis_match;	//mis_match
		// 		}
		// 		if(matrix[recall_x-1][recall_y-1]+d_score==matrix[recall_x][recall_y])	//匹配，对角线方向回溯
		// 		{
		// 			l1[s1_l]=a1[recall_y-1];
		// 			l2[s2_l]=a2[recall_x-1];
		// 			recall_x--;
		// 			recall_y--;
		// 			count++;
		// 			s1_l++;
		// 			s2_l++;
		// 			continue;
		// 		}
		// 		else{
		// 			if(matrix[recall_x-1][recall_y]+d==matrix[recall_x][recall_y])		//未匹配，向上回溯
		// 			{
		// 				l1[s1_l]='_';
		// 				l2[s2_l]=a2[recall_x-1];
		// 				recall_x--;
		// 				count++;
		// 				s1_l++;
		// 				s2_l++;
		// 				continue;
		// 			}
		// 			else{
		// 				l1[s1_l]=a1[recall_y-1];										//未匹配，向左回溯
		// 				l2[s2_l]='_';
		// 				recall_y--;
		// 				count++;
		// 				s1_l++;
		// 				s2_l++;
		// 				continue;
		// 			}
				
		// 		}
		// 	}
		// 	l1[count]='\0';	//比对结果序列收尾
		// 	l2[count]='\0';	
		// 	fun(l1);		//比对结果序列顺序纠正
		// 	fun(l2);
		// }
		// MPI_Barrier(MPI_COMM_WORLD);
/////////////////////////////////////!!!!回溯结束!!!!////////////////////////////////////

		time_over2=clock();			//计时2结束
		ed2=rpcc();

/////////////////////////////////////!!!!写入文件开始!!!!////////////////////////////////////
		// MPI_Barrier(MPI_COMM_WORLD);
		// for(i=0;i<seq_num;i++)
		// {
		// if(mpi_id==i)					//由0号进程对比对结果进行写回操作
		// {	
		// 	FILE *fq=NULL;
		// 	fq=fopen("result.txt","a+");
		// 	char *first="The result is:\n";
		// 	if(fq==NULL)
		// 	{
		// 		printf("can't open file\n");
		// 	}
		// 	else
		// 	{
		// 		char *first="The result is:\n";
		// 		fputs(first,fq);
		// 		int num,len1,len2,sub;
		// 		int cut_n=count/64+1;	//以每64个字符一行对比对结果进行打印
		// 		int local;
		// 		for(num=1;num<=cut_n;num++)
		// 		{
		// 			int sub;
		// 			char p1[65],p2[65];
		// 			local=(num-1)*64;
		// 			int cut=64;
		// 			if(num*64>count)
		// 			{
		// 				cut=count-local;
		// 			}
		// 			char chip_s[cut+1];
		// 			for(sub=0;sub<cut;sub++)
		// 			{
		// 				chip_s[sub]='|';
		// 			}
		// 			chip_s[cut]='\0';
		// 			strncpy(p1,l1+local,cut);
		// 			p1[cut]='\0';
		// 			strncpy(p2,l2+local,cut);
		// 			p2[cut]='\0';
		// 			fputs(p1,fq);
		// 			fputs("\n",fq);
		// 			fputs(chip_s,fq);
		// 			fputs("\n",fq);
		// 			fputs(p2,fq);
		// 			fputs("\n\n",fq);
		// 		}
		// 	}
		// }
		// }
		// MPI_Barrier(MPI_COMM_WORLD);
/////////////////////////////////////!!!!写入文件结束!!!!////////////////////////////////////
		MPI_Barrier(MPI_COMM_WORLD);
		ed1=rpcc();
		time_over1=clock();			//计时1结束
		run_time=(double)(ed1-st1)/1450000000;
		run_time2=(double)(ed2-st2)/1450000000;
		run_time1=(double)(time_over1-time_start1)/CLOCKS_PER_SEC;
		//double run_time22=(double)(time_over2-time_start2)/CLOCKS_PER_SEC;
		run_time3=(double)(time_over3-time_start3)/CLOCKS_PER_SEC;
		if(mpi_id == 0)	//0号进程对计时结果进行打印
		{
			printf("the manycore counter=%d\n",(ed2-st2));						//????
			printf("the total run time:%fs\n",run_time);						//计时1包含了打分矩阵的计算与回溯以及文件的读写的耗时
			printf("the running time of the algorithm=%fs\n",run_time2);		//计时2包含了打分矩阵的计算与回溯的耗时
			//printf("the running time of the algorithm2=%fs\n",run_time22);		//计时2包含了打分矩阵的计算与回溯的耗时
			printf("the running time of calculating matrix=%fs\n",run_time3);	//计时3只包含了打分矩阵的计算部分用时
		}
	}
    MPI_Finalize();
	return 0;
}