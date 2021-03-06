/* hack_tcp.c  Created on: 18.05.2012    Author: Alex  */
//

#include "hack_tcp.h"


THeader Header;   // ���������� ��������� ���������

alt_sgdma_descriptor DescHeader __attribute__ ((section ("desc")  ) );
alt_sgdma_descriptor DescData  __attribute__ ((section ("desc")  ) );
alt_sgdma_descriptor DescEnd  __attribute__ ((section ("desc")  ) );



ushort swap2(ushort arg){ return( ( (arg & 0x00ff)<<8) + ((arg & 0xff00)>>8) ); }

unsigned short checksum(ushort *buffer, int size){
//��� IP � ICMP
   unsigned long cksum=0;
   ushort res;
   while(size > 1) { cksum+=*buffer++; size -= 2; }
   if(size) cksum += *(unsigned char *)buffer;
   cksum = (cksum >> 16) + (cksum & 0xffff);
   cksum += (cksum >>16);
   res = ~cksum ;
   return ( res );
}

void  InitUDP(  ){
	   Header.UdpHeader.ud_cksum=0;  // ���� �� ����������
	   Header.UdpHeader.ud_srcp=htons(54035);  // ������ ������ ���������
	   Header.UdpHeader.ud_dstp=htons(54035);
	   Header.UdpHeader.ud_len=swap2(BlockSize+sizeof( TUdpHeader) );

	   Header.IpHeader.ip_ver_ihl=0x45;  // ����� ��������� � ������
	   Header.IpHeader.ip_tos=0;
	   Header.IpHeader.ip_len=swap2(BlockSize+sizeof(TUdpHeader)+sizeof(TIpHeader) );
	   Header.IpHeader.ip_id=0;   // ��� ���
	   Header.IpHeader.ip_flgs_foff=0; // ������ �������
	   Header.IpHeader.ip_time=2;      // ����� ����� ������
	   Header.IpHeader.ip_prot=0x11;   //��� ��������� udp
	   Header.IpHeader.ip_chksum= 0;
	   Header.IpHeader.ip_src=192L + (168L<<8) + (0L<<16) + (55L<<24);  // ip ����� ���� ���������
	   Header.IpHeader.ip_dest=192L + (168L<<8) + (0L<<16) + (3L<<24);
	   Header.IpHeader.ip_chksum = checksum( (ushort *)&Header.IpHeader, 20); //!!!!!!!!

	   // ������, ��� ���� mac �������� �� �����
	   Header.MacHeader.st=0x0000;
	   Header.MacHeader.dest_mac0=0x00;
	   Header.MacHeader.dest_mac1=0x24;
	   Header.MacHeader.dest_mac2=0x81;
	   Header.MacHeader.dest_mac3=0xb1;
	   Header.MacHeader.dest_mac4=0xa8;
	   Header.MacHeader.dest_mac5=0xee;
	   Header.MacHeader.source_mac0=0x00;
	   Header.MacHeader.source_mac1=0x25;
	   Header.MacHeader.source_mac2=0x02;
	   Header.MacHeader.source_mac3=0x03;
	   Header.MacHeader.source_mac4=0x04;
	   Header.MacHeader.source_mac5=0x07;
	   Header.MacHeader.type_len=0x0008;


		 // ������� ���� �����������
		   alt_avalon_sgdma_construct_mem_to_stream_desc(
		       &DescHeader,
		       &DescEnd,// pointer to "next"
		      (alt_u32*) &Header,                     // starting read address
		      sizeof(THeader),                                  // # bytes
		      0,                                        // don't read from constant address
		      1,                                        // generate sop
		      0,                                        // generate endofpacket signal
		      0);                                       // atlantic channel (don't know/don't care: set to 0)

		   alt_avalon_sgdma_construct_mem_to_stream_desc(
		       &DescData,
		      &DescEnd,// pointer to "next"
		      (alt_u32*) NULL,                     // starting read address
		      BlockSize,                                  // # bytes
		      0,                                        // don't read from constant address
		      0,                                        // generate sop
		      1,                                        // generate endofpacket signal
		      0);                                       // atlantic channel (don't know/don't care: set to 0)


   }

// ��. ������� ���� �������������� ��� ������ � ����� ������������� �� ���� �����

alt_32 hack_tse_mac_sTxWrite( tse_mac_trans_info *mi,  alt_sgdma_descriptor *txDesc){

  alt_32 timeout;
  alt_u8 result = 0;
  alt_u16 actualBytesTransferred;

  // Make sure DMA controller is not busy from a former command
  // and TX is able to accept data
  timeout = 0;
  //tse_dprintf("\nWaiting while tx SGDMA is busy......... ");
  while ( (IORD_ALTERA_AVALON_SGDMA_STATUS(mi->tx_sgdma->base) &
           ALTERA_AVALON_SGDMA_STATUS_BUSY_MSK) ) {
           if(timeout++ == ALTERA_TSE_SGDMA_BUSY_TIME_OUT_CNT) {
            tse_dprintf(4, "WARNING : TX SGDMA Timeout\n");
            return ENP_RESOURCE;  // avoid being stuck here
           }
  }

  // Set up the SGDMA
  // Clear the status and control bits of the SGDMA descriptor
  IOWR_ALTERA_AVALON_SGDMA_CONTROL (mi->tx_sgdma->base, 0);
  IOWR_ALTERA_AVALON_SGDMA_STATUS (mi->tx_sgdma->base, 0xFF);

  // Start SGDMA (blocking call)
  result = alt_avalon_sgdma_do_sync_transfer(   mi->tx_sgdma,  (alt_sgdma_descriptor *) &txDesc[0]    );

  /* perform cache save read to obtain actual bytes transferred for current sgdma descriptor */
  actualBytesTransferred = IORD_ALTERA_TSE_SGDMA_DESC_ACTUAL_BYTES_TRANSFERRED(&txDesc[0]);

  return actualBytesTransferred;
}


int hack_tse_mac_raw_send(NET net, char * data, unsigned data_bytes, int sop_eop)
{
   int result,i,tx_length;
   unsigned len = data_bytes;

   ins_tse_info* tse_ptr = (ins_tse_info*) net->n_local;

   alt_tse_system_info* tse_hw = (alt_tse_system_info *) tse_ptr->tse;

   tse_mac_trans_info *mi;
   unsigned int* ActualData;
   int cpu_sr;

   OS_ENTER_CRITICAL();
   mi = &tse_ptr->mi;

   if(tse_ptr->sem!=0) /* Tx is busy*/
   {
      dprintf("raw_send CALLED AGAIN(hack)!!!\n");
      OS_EXIT_CRITICAL();
      return ENP_RESOURCE;
   }

   tse_ptr->sem = 1;

      ActualData = (unsigned int*)data;  /* base driver will detect 16-bit shift. */
     // clear bit-31 before passing it to SGDMA Driver
     ActualData = (unsigned int*)alt_remap_cached ((volatile void*) ActualData, 4);

   /* Write data to Tx FIFO using the DMA */

  /*
    if((tse_hw->use_shared_fifo == 1) && (( len > ALTERA_TSE_MIN_MTU_SIZE )) && (IORD_ALTERA_MULTI_CHAN_FILL_LEVEL(tse_hw->tse_shared_fifo_tx_stat_base, tse_ptr->channel) < ALTERA_TSE_MIN_MTU_SIZE))
   {

        alt_avalon_sgdma_construct_mem_to_stream_desc(
           (alt_sgdma_descriptor *) &tse_ptr->desc[ALTERA_TSE_FIRST_TX_SGDMA_DESC_OFST], // descriptor I want to work with
           (alt_sgdma_descriptor *) &tse_ptr->desc[ALTERA_TSE_SECOND_TX_SGDMA_DESC_OFST],// pointer to "next"
           (alt_u32*)ActualData,                     // starting read address
           (len),                                  // # bytes
           0,                                        // don't read from constant address
           sop_eop?1:0,                                        // generate sop
           sop_eop?0:1,                                        //  ��� !!!!!!!!!generate endofpacket signal
           0);                                       // atlantic channel (don't know/don't care: set to 0)

        tx_length = tse_mac_sTxWrite(mi,tse_ptr->desc);
        result = 0;
   }
   else if( len > ALTERA_TSE_MIN_MTU_SIZE ) {

        alt_avalon_sgdma_construct_mem_to_stream_desc(
           (alt_sgdma_descriptor *) &tse_ptr->desc[ALTERA_TSE_FIRST_TX_SGDMA_DESC_OFST], // descriptor I want to work with
           (alt_sgdma_descriptor *) &tse_ptr->desc[ALTERA_TSE_SECOND_TX_SGDMA_DESC_OFST],// pointer to "next"
           (alt_u32*)ActualData,                     // starting read address
           (len),                                  // # bytes
           0,                                        // don't read from constant address
           sop_eop?1:0,                                        // generate sop
           sop_eop?0:1,                                        // generate endofpacket signal
           0);                                       // atlantic channel (don't know/don't care: set to 0)


       tx_length = tse_mac_sTxWrite(mi,tse_ptr->desc);
       result = 0;

   } else {
       result = -3;
   }
*/
   alt_avalon_sgdma_construct_mem_to_stream_desc(
      (alt_sgdma_descriptor *) &tse_ptr->desc[ALTERA_TSE_FIRST_TX_SGDMA_DESC_OFST], // descriptor I want to work with
      (alt_sgdma_descriptor *) &tse_ptr->desc[ALTERA_TSE_SECOND_TX_SGDMA_DESC_OFST],// pointer to "next"
      (alt_u32*)ActualData,                     // starting read address
      (len),                                  // # bytes
      0,                                        // don't read from constant address
      sop_eop?1:0,                                        // generate sop
      sop_eop?0:1,                                        // generate endofpacket signal
      0);                                       // atlantic channel (don't know/don't care: set to 0)


  tx_length = hack_tse_mac_sTxWrite(mi,tse_ptr->desc);
  result = 0;

   if(result < 0)   /* SGDMA not available */
   {
      dprintf("raw_send() SGDMA not available, ret=%d, len=%d\n",result, len);
      net->n_mib->ifOutDiscards++;
      tse_ptr->sem = 0;

      OS_EXIT_CRITICAL();
      return SEND_DROPPED;   /* ENP_RESOURCE and SEND_DROPPED have the same value! */
   }
   else   /* = 0, success */
   {
      net->n_mib->ifOutOctets += data_bytes;
      /* we dont know whether it was unicast or not, we count both in <ifOutUcastPkts> */
      net->n_mib->ifOutUcastPkts++;
      tse_ptr->sem = 0;

      OS_EXIT_CRITICAL();
      return SUCCESS;  /*success */
   }
}

 void hack_send( NET net,  unsigned char *data, int Len ){
	 // ��������! ������ � ����������� �����������

   ins_tse_info* tse_ptr = (ins_tse_info*) net->n_local;
   //alt_tse_system_info* tse_hw = (alt_tse_system_info *) tse_ptr->tse;
   tse_mac_trans_info *mi;
   alt_u8 result=0;
   alt_u8 control_header,control_data;
   alt_u8 *PData=data;
   int cpu_sr,tx_length;
   alt_32 timeout;

   OS_ENTER_CRITICAL();
   mi = &tse_ptr->mi;
   // ���� ������, ��������� ?
   if(tse_ptr->sem!=0){
      dprintf("raw_send CALLED AGAIN!!!\n");
      OS_EXIT_CRITICAL();
      return ;
   }
   tse_ptr->sem = 1; // ���������
   OS_EXIT_CRITICAL();

   control_header=DescHeader.control;
   control_data=DescData.control;


	   do{
	   // ��������� �������
          IOWR_32DIRECT(&DescData.read_addr, 0, (alt_u32)PData);

		   OS_ENTER_CRITICAL();
		   tx_length = hack_tse_mac_sTxWrite(mi,  &DescHeader);
		   tx_length = hack_tse_mac_sTxWrite(mi,  &DescData);
          // result = alt_avalon_sgdma_do_sync_transfer(mi->tx_sgdma, (alt_sgdma_descriptor *) &DescHeader  );
          // result = alt_avalon_sgdma_do_sync_transfer(mi->tx_sgdma, (alt_sgdma_descriptor *) &DescData  );
	       Len-=BlockSize; PData+=BlockSize;

	       // "�����������"
		   //IOWR_ALTERA_AVALON_SGDMA_CONTROL (mi->tx_sgdma->base, 0);
		   //IOWR_ALTERA_AVALON_SGDMA_STATUS (mi->tx_sgdma->base, 0xFF);
	         // "�����������" ������������
	         IOWR_8DIRECT(&DescHeader.control,0,control_header);  // �������� ������ ���������
	         IOWR_8DIRECT(&DescHeader.status,0,0x00);
	         IOWR_32DIRECT(&DescData.read_addr, 0, (alt_u32)PData);
	         IOWR_8DIRECT (&DescData.control,0,control_data);  // �������� ������ ���������
	         IOWR_8DIRECT (&DescData.status,0,0x00);

            OS_EXIT_CRITICAL();
            OS_Sched();
	   } while(Len>0);

	   OS_ENTER_CRITICAL();
       tse_ptr->sem = 0;
	   OS_EXIT_CRITICAL();
}



 void SendUDP(alt_u8 *PBuf, int Len, NET *ExtNet){
     int res;
   //  hack_send( ExtNet,  PBuf, Len );

	 do{
    	 res= hack_tse_mac_raw_send( ExtNet,(char*) &Header, UDPHeaderSize,1 );
    	 res= hack_tse_mac_raw_send( ExtNet,(char*) PBuf, BlockSize,0 );
		 PBuf+=BlockSize;
		 Len-=BlockSize;
//		 OS_Sched();
//		 OSTimeDly(1);
	 }while(Len>0);

 }


