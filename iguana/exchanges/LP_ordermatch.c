
/******************************************************************************
 * Copyright © 2014-2017 The SuperNET Developers.                             *
 *                                                                            *
 * See the AUTHORS, DEVELOPER-AGREEMENT and LICENSE files at                  *
 * the top-level directory of this distribution for the individual copyright  *
 * holder information and the developer policies on copyright and licensing.  *
 *                                                                            *
 * Unless otherwise agreed in a custom licensing agreement, no part of the    *
 * SuperNET software, including this file may be copied, modified, propagated *
 * or distributed except according to the terms contained in the LICENSE file *
 *                                                                            *
 * Removal or modification of this copyright notice is prohibited.            *
 *                                                                            *
 ******************************************************************************/

//
//  LP_ordermatch.c
//  marketmaker
//
struct LP_quoteinfo LP_Alicequery,LP_Alicereserved;
double LP_Alicemaxprice;
bits256 LP_Alicedestpubkey,LP_bobs_reserved;
uint32_t Alice_expiration,Bob_expiration;
struct { uint64_t aliceid; double bestprice; uint32_t starttime,counter; } Bob_competition[512];

double LP_bob_competition(int32_t *counterp,uint64_t aliceid,double price,int32_t counter)
{
    int32_t i,firsti = -1; uint32_t now = (uint32_t)time(NULL);
    *counterp = 0;
    for (i=0; i<sizeof(Bob_competition)/sizeof(*Bob_competition); i++)
    {
        if ( Bob_competition[i].aliceid == aliceid )
        {
            if ( counter < 0 || now > Bob_competition[i].starttime+LP_AUTOTRADE_TIMEOUT )
            {
                //printf("aliceid.%llu expired\n",(long long)aliceid);
                Bob_competition[i].bestprice = 0.;
                Bob_competition[i].starttime = now;
                Bob_competition[i].counter = 0;
            }
            if ( price != 0. && (Bob_competition[i].bestprice == 0. || price < Bob_competition[i].bestprice) )
            {
                Bob_competition[i].bestprice = price;
                //printf("Bob competition aliceid.%llu <- bestprice %.8f\n",(long long)aliceid,price);
            }
            Bob_competition[i].counter += counter;
            *counterp = Bob_competition[i].counter;
            return(Bob_competition[i].bestprice);
        }
        else if ( Bob_competition[i].aliceid == 0 )
            firsti = i;
    }
    if ( firsti < 0 )
        firsti = (LP_rand() % (sizeof(Bob_competition)/sizeof(*Bob_competition)));
    Bob_competition[firsti].starttime = (uint32_t)time(NULL);
    Bob_competition[firsti].counter = counter;
    Bob_competition[firsti].aliceid = aliceid;
    Bob_competition[firsti].bestprice = price;
    *counterp = counter;
    //printf("Bob competition aliceid.%llu %.8f\n",(long long)aliceid,price);
    return(price);
}

uint64_t LP_txfeecalc(struct iguana_info *coin,uint64_t txfee,int32_t txlen)
{
    if ( coin != 0 )
    {
        if ( strcmp(coin->symbol,"BTC") == 0 )
        {
            if ( txlen == 0 )
                txlen = LP_AVETXSIZE;
            coin->rate = LP_getestimatedrate(coin);
            if ( (txfee= SATOSHIDEN * coin->rate * txlen) <= LP_MIN_TXFEE )
            {
                coin->rate = -1.;
                coin->rate = _LP_getestimatedrate(coin);
                if ( (txfee= SATOSHIDEN * coin->rate * txlen) <= LP_MIN_TXFEE )
                    txfee = LP_MIN_TXFEE;
            }
        } else txfee = coin->txfee;
        if ( txfee < LP_MIN_TXFEE )
            txfee = LP_MIN_TXFEE;
    }
    return(txfee);
}

int32_t LP_quote_checkmempool(struct LP_quoteinfo *qp,struct LP_utxoinfo *autxo,struct LP_utxoinfo *butxo)
{
    int32_t selector,spendvini; bits256 spendtxid;
    if ( butxo != 0 && (selector= LP_mempool_vinscan(&spendtxid,&spendvini,qp->srccoin,qp->coinaddr,qp->txid,qp->vout,qp->txid2,qp->vout2)) >= 0 )
    {
        char str[65]; printf("LP_tradecommand selector.%d in mempool %s vini.%d",selector,bits256_str(str,spendtxid),spendvini);
        return(-1);
    }
    if ( autxo != 0 && (selector= LP_mempool_vinscan(&spendtxid,&spendvini,qp->destcoin,qp->destaddr,qp->desttxid,qp->destvout,qp->feetxid,qp->feevout)) >= 0 )
    {
        char str[65]; printf("LP_tradecommand dest selector.%d in mempool %s vini.%d",selector,bits256_str(str,spendtxid),spendvini);
        return(-1);
    }
    return(0);
}

double LP_quote_validate(struct LP_utxoinfo *autxo,struct LP_utxoinfo *butxo,struct LP_quoteinfo *qp,int32_t iambob)
{
    double qprice=0.; char str[65]; cJSON *txout; uint64_t txfee,desttxfee,srcvalue=0,srcvalue2=0,destvalue=0,destvalue2=0;
    //printf(">>>>>>> quote satoshis.(%.8f %.8f) %s %.8f -> %s %.8f\n",dstr(qp->satoshis),dstr(qp->destsatoshis),qp->srccoin,dstr(qp->satoshis),qp->destcoin,dstr(qp->destsatoshis));
    if ( butxo != 0 )
    {
        if ( LP_iseligible(&srcvalue,&srcvalue2,1,qp->srccoin,qp->txid,qp->vout,qp->satoshis,qp->txid2,qp->vout2) == 0 )
        {
            //printf("bob not eligible %s (%.8f %.8f)\n",jprint(LP_quotejson(qp),1),dstr(srcvalue),dstr(srcvalue2));
            return(-2);
        }
        if ( (txout= LP_gettxout(qp->srccoin,qp->coinaddr,qp->txid,qp->vout)) != 0 )
            free_json(txout);
        else
        {
            printf("%s %s payment %s/v%d is spent\n",qp->srccoin,qp->coinaddr,bits256_str(str,qp->txid),qp->vout);
            return(-21);
        }
        if ( (txout= LP_gettxout(qp->srccoin,qp->coinaddr,qp->txid2,qp->vout2)) != 0 )
            free_json(txout);
        else
        {
            printf("%s %s deposit %s/v%d is spent\n",qp->srccoin,qp->coinaddr,bits256_str(str,qp->txid2),qp->vout2);
            return(-22);
        }
        if ( bits256_cmp(butxo->deposit.txid,qp->txid2) != 0 || butxo->deposit.vout != qp->vout2 )
        {
            char str[65],str2[65]; printf("%s != %s v%d != %d\n",bits256_str(str,butxo->deposit.txid),bits256_str(str2,qp->txid2),butxo->deposit.vout,qp->vout2);
            return(-6);
        }
        if ( strcmp(butxo->coinaddr,qp->coinaddr) != 0 )
        {
            printf("(%s) != (%s)\n",butxo->coinaddr,qp->coinaddr);
            return(-7);
        }
    }
    if ( autxo != 0 )
    {
        if ( LP_iseligible(&destvalue,&destvalue2,0,qp->destcoin,qp->desttxid,qp->destvout,qp->destsatoshis,qp->feetxid,qp->feevout) == 0 )
        {
            //alice not eligible 0.36893923 -> dest 0.55020000 1.49130251 (0.61732249 0.00104324) 14b8b74808d2d34a70e5eddd1cad47d855858f8b23cac802576d4d37b5f8af8f/v1 abec6e76169bcb738235ca67fab02cc55390f39e422aa71f1badf8747c290cc4/v1
            //char str[65],str2[65]; printf("alice not eligible %.8f -> dest %.8f %.8f (%.8f %.8f) %s/v%d %s/v%d\n",dstr(qp->satoshis),dstr(qp->destsatoshis),(double)qp->destsatoshis/qp->satoshis,dstr(destvalue),dstr(destvalue2),bits256_str(str,qp->desttxid),qp->destvout,bits256_str(str2,qp->feetxid),qp->feevout);
            return(-3);
        }
        if ( (txout= LP_gettxout(qp->destcoin,qp->destaddr,qp->desttxid,qp->destvout)) != 0 )
            free_json(txout);
        else
        {
            printf("%s %s Apayment %s/v%d is spent\n",qp->destcoin,qp->destaddr,bits256_str(str,qp->desttxid),qp->destvout);
            return(-23);
        }
        if ( (txout= LP_gettxout(qp->destcoin,qp->destaddr,qp->feetxid,qp->feevout)) != 0 )
            free_json(txout);
        else
        {
            printf("%s %s dexfee %s/v%d is spent\n",qp->destcoin,qp->destaddr,bits256_str(str,qp->feetxid),qp->feevout);
            return(-24);
        }
    }
    //printf("checked autxo and butxo\n");
    if ( LP_quote_checkmempool(qp,autxo,butxo) < 0 )
        return(-4);
    if ( iambob == 0 && autxo != 0 )
    {
        if ( bits256_cmp(autxo->fee.txid,qp->feetxid) != 0 || autxo->fee.vout != qp->feevout )
            return(-9);
        if ( strcmp(autxo->coinaddr,qp->destaddr) != 0 )
            return(-10);
    }
    if ( autxo != 0 && destvalue < qp->desttxfee+qp->destsatoshis )
    {
        printf("destvalue %.8f  destsatoshis %.8f is too small txfee %.8f?\n",dstr(destvalue),dstr(qp->destsatoshis),dstr(qp->desttxfee));
        return(-11);
    }
    if ( butxo != 0 && srcvalue < qp->txfee+qp->satoshis )
    {
        printf("srcvalue %.8f [%.8f] satoshis %.8f is too small txfee %.8f?\n",dstr(srcvalue),dstr(srcvalue) - dstr(qp->txfee+qp->satoshis),dstr(qp->satoshis),dstr(qp->txfee));
        return(-33);
    }
    if ( qp->satoshis != 0 )
        qprice = ((double)qp->destsatoshis / (qp->satoshis-qp->txfee));
    LP_txfees(&txfee,&desttxfee,qp->srccoin,qp->destcoin);
    if ( txfee < qp->txfee )
        txfee = qp->txfee;
    if ( desttxfee < qp->desttxfee )
        desttxfee = qp->desttxfee;
    //printf("qprice %.8f <- %.8f/%.8f txfees.(%.8f %.8f) vs (%.8f %.8f)\n",qprice,dstr(qp->destsatoshis),dstr(qp->satoshis),dstr(qp->txfee),dstr(qp->desttxfee),dstr(txfee),dstr(desttxfee));
    if ( qp->txfee < LP_REQUIRED_TXFEE*txfee || qp->desttxfee < LP_REQUIRED_TXFEE*desttxfee )
        return(-14);
    if ( butxo != 0 )
    {
        if ( qp->satoshis < (srcvalue / LP_MINVOL) || srcvalue < qp->txfee*LP_MINSIZE_TXFEEMULT )
        {
            printf("utxo payment %.8f is less than %f covered by Q %.8f or <10x txfee %.8f [%d %d]\n",dstr(srcvalue),1./LP_MINVOL,dstr(qp->satoshis),dstr(qp->txfee),qp->satoshis < (srcvalue / LP_MINVOL),srcvalue < qp->txfee*LP_MINSIZE_TXFEEMULT);
            return(-12);
        }
    }
    if ( autxo != 0 )
    {
        if ( qp->destsatoshis < (destvalue / LP_MINCLIENTVOL) || destvalue < qp->desttxfee*LP_MINSIZE_TXFEEMULT )
        {
            printf("destsatoshis %.8f is less than %f of value %.8f or < 10x txfee %.8f\n",dstr(qp->destsatoshis),1./LP_MINCLIENTVOL,dstr(destvalue),dstr(qp->desttxfee));
            return(-13);
        }
    }
    return(qprice);
}

int32_t LP_arrayfind(cJSON *array,bits256 txid,int32_t vout)
{
    int32_t i,n = cJSON_GetArraySize(array); cJSON *item;
    for (i=0; i<n; i++)
    {
        item = jitem(array,i);
        if ( vout == jint(item,"vout") && bits256_cmp(txid,jbits256(item,"txid")) == 0 )
            return(i);
    }
    return(-1);
}

int32_t LP_nanobind(void *ctx,char *pairstr)
{
    int32_t i,r,pairsock = -1; uint16_t mypullport; char bindaddr[128];
    if ( LP_canbind != 0 )
    {
        if ( (pairsock= nn_socket(AF_SP,NN_PAIR)) < 0 )
            printf("error creating utxo->pair\n");
        else
        {
            for (i=0; i<10; i++)
            {
                r = (10000 + (LP_rand() % 50000)) & 0xffff;
                if ( LP_fixed_pairport != 0 )
                    r = LP_fixed_pairport;
                nanomsg_transportname(0,pairstr,LP_myipaddr,r);
                nanomsg_transportname(1,bindaddr,LP_myipaddr,r);
                if ( nn_bind(pairsock,bindaddr) >= 0 )
                {
                    //timeout = 1;
                    //nn_setsockopt(pairsock,NN_SOL_SOCKET,NN_SNDTIMEO,&timeout,sizeof(timeout));
                    //nn_setsockopt(pairsock,NN_SOL_SOCKET,NN_RCVTIMEO,&timeout,sizeof(timeout));
                    printf("nanobind %s to %d\n",pairstr,pairsock);
                    return(pairsock);
                } else printf("error binding to %s for %s\n",bindaddr,pairstr);
                if ( LP_fixed_pairport != 0 )
                    break;
            }
        }
    } else pairsock = LP_initpublicaddr(ctx,&mypullport,pairstr,"127.0.0.1",0,1);
    return(pairsock);
}

int32_t LP_nearest_utxovalue(struct iguana_info *coin,char *coinaddr,struct LP_address_utxo **utxos,int32_t n,uint64_t targetval)
{
    int32_t i,replacei,bestheight,mini = -1; struct LP_address_utxo *up,*bestup; struct electrum_info *backupep=0,*ep; int64_t dist,bestdist; uint64_t mindist = (1LL << 60);
    if ( (ep= coin->electrum) != 0 )
    {
        if ( (backupep= ep->prev) == 0 )
            backupep = ep;
    }
    //printf("LP_nearest_utxovalue %s %s utxos[%d] target %.8f\n",coin->symbol,coinaddr,n,dstr(targetval));
    for (i=0; i<n; i++)
    {
        if ( (up= utxos[i]) != 0 )
        {
            dist = (up->U.value - targetval);
            //printf("nearest i.%d target %.8f val %.8f dist %.8f mindist %.8f mini.%d spent.%d\n",i,dstr(targetval),dstr(up->U.value),dstr(dist),dstr(mindist),mini,up->spendheight);
            if ( up->spendheight <= 0 )
            {
                if ( dist >= 0 && dist < mindist )
                {
                    //printf("(%.8f %.8f %.8f).%d ",dstr(up->U.value),dstr(dist),dstr(mindist),mini);
                    mini = i;
                    mindist = dist;
                }
            }
        }
    }
    if ( mini >= 0 && (bestup= utxos[mini]) != 0 )
    {
        bestdist = (bestup->U.value - targetval);
        replacei = -1;
        bestheight = bestup->U.height;
        for (i=0; i<n; i++)
        {
            if ( i != mini && (up= utxos[i]) != 0 )
            {
                dist = (up->U.value - targetval);
                if ( dist > 0 && up->U.height < bestheight )
                {
                    if ( (double)dist/bestdist < sqrt(((double)bestheight - up->U.height)/1000) )
                    {
                        replacei = i;
                        bestheight = up->U.height;
                    } //else printf("almost ratio %.3f dist %.8f vs best %.8f, ht %d vs best ht %d\n",(double)dist/bestdist,dstr(dist),dstr(bestdist),up->U.height,bestheight);
                }
            }
        }
        if ( replacei >= 0 )
        {
            printf("REPLACE bestdist %.8f height %d with dist %.8f height %d\n",dstr(bestdist),bestup->U.height,dstr(utxos[replacei]->U.value - targetval),utxos[replacei]->U.height);
            return(replacei);
        }
    }
    //printf("return mini.%d\n",mini);
    return(mini);
}

void LP_butxo_set(struct LP_utxoinfo *butxo,int32_t iambob,struct iguana_info *coin,struct LP_address_utxo *up,struct LP_address_utxo *up2,int64_t satoshis)
{
    butxo->pubkey = G.LP_mypub25519;
    safecopy(butxo->coin,coin->symbol,sizeof(butxo->coin));
    safecopy(butxo->coinaddr,coin->smartaddr,sizeof(butxo->coinaddr));
    butxo->payment.txid = up->U.txid;
    butxo->payment.vout = up->U.vout;
    butxo->payment.value = up->U.value;
    if ( (butxo->iambob= iambob) != 0 )
    {
        butxo->deposit.txid = up2->U.txid;
        butxo->deposit.vout = up2->U.vout;
        butxo->deposit.value = up2->U.value;
    }
    else
    {
        butxo->fee.txid = up2->U.txid;
        butxo->fee.vout = up2->U.vout;
        butxo->fee.value = up2->U.value;
    }
    butxo->swap_satoshis = satoshis;
}

void LP_abutxo_set(struct LP_utxoinfo *autxo,struct LP_utxoinfo *butxo,struct LP_quoteinfo *qp)
{
    if ( butxo != 0 )
    {
        memset(butxo,0,sizeof(*butxo));
        butxo->pubkey = qp->srchash;
        safecopy(butxo->coin,qp->srccoin,sizeof(butxo->coin));
        safecopy(butxo->coinaddr,qp->coinaddr,sizeof(butxo->coinaddr));
        butxo->payment.txid = qp->txid;
        butxo->payment.vout = qp->vout;
        //butxo->payment.value = qp->value;
        butxo->iambob = 1;
        butxo->deposit.txid = qp->txid2;
        butxo->deposit.vout = qp->vout2;
        //butxo->deposit.value = up2->U.value;
        butxo->swap_satoshis = qp->satoshis;
    }
    if ( autxo != 0 )
    {
        memset(autxo,0,sizeof(*autxo));
        autxo->pubkey = qp->desthash;
        safecopy(autxo->coin,qp->destcoin,sizeof(autxo->coin));
        safecopy(autxo->coinaddr,qp->destaddr,sizeof(autxo->coinaddr));
        autxo->payment.txid = qp->desttxid;
        autxo->payment.vout = qp->destvout;
        //autxo->payment.value = qp->value;
        autxo->iambob = 0;
        autxo->fee.txid = qp->feetxid;
        autxo->fee.vout = qp->feevout;
        //autxo->deposit.value = up2->U.value;
        autxo->swap_satoshis = qp->destsatoshis;
    }
}

uint64_t LP_basesatoshis(double relvolume,double price,uint64_t txfee,uint64_t desttxfee)
{
    //printf("basesatoshis %.8f (rel %.8f / price %.8f)\n",dstr(SATOSHIDEN * ((relvolume) / price) + 2*txfee),relvolume,price);
    if ( relvolume > dstr(desttxfee) && price > SMALLVAL )
        return(SATOSHIDEN * (relvolume / price) + 2*txfee);
    else return(0);
}

struct LP_utxoinfo *LP_address_myutxopair(struct LP_utxoinfo *butxo,int32_t iambob,struct LP_address_utxo **utxos,int32_t max,struct iguana_info *coin,char *coinaddr,uint64_t txfee,double relvolume,double price,uint64_t desttxfee)
{
    struct LP_address *ap; uint64_t fee,targetval,targetval2; int32_t m,mini; struct LP_address_utxo *up,*up2; double ratio;
    memset(butxo,0,sizeof(*butxo));
    if ( iambob != 0 )
    {
        targetval = LP_basesatoshis(relvolume,price,txfee,desttxfee) + 3*txfee;
        targetval2 = (targetval / 8) * 9 + 3*txfee;
        fee = txfee;
        ratio = LP_MINVOL;
    }
    else
    {
        targetval = relvolume*SATOSHIDEN + 3*desttxfee;
        targetval2 = (targetval / 777) + 3*desttxfee;
        fee = desttxfee;
        ratio = LP_MINCLIENTVOL;
    }
    if ( coin != 0 && (ap= LP_address(coin,coinaddr)) != 0 )
    {
        if ( (m= LP_address_utxo_ptrs(coin,iambob,utxos,max,ap,coinaddr)) > 1 )
        {
            if ( 0 )
            {
                int32_t i;
                for (i=0; i<m; i++)
                    if ( utxos[i]->U.value >= targetval )
                        printf("%.8f ",dstr(utxos[i]->U.value));
                printf("targetval %.8f vol %.8f price %.8f txfee %.8f %s %s\n",dstr(targetval),relvolume,price,dstr(fee),coin->symbol,coinaddr);
            }
            while ( 1 )
            {
                mini = -1;
                if ( targetval != 0 && (mini= LP_nearest_utxovalue(coin,coinaddr,utxos,m,targetval+fee)) >= 0 )
                {
                    up = utxos[mini];
                    utxos[mini] = 0;
                    printf("found mini.%d %.8f for targetval %.8f -> targetval2 %.8f, ratio %.2f\n",mini,dstr(up->U.value),dstr(targetval),dstr(targetval2),(double)up->U.value/targetval);
                    if ( (double)up->U.value/targetval < ratio-1 )
                        
                    {
                        if ( 0 )
                        {
                            int32_t i;
                            for (i=0; i<m; i++)
                                if ( utxos[i] != 0 && utxos[i]->U.value >= targetval2 )
                                    printf("%.8f ",dstr(utxos[i]->U.value));
                            printf("targetval2 %.8f vol %.8f price %.8f txfee %.8f %s %s\n",dstr(targetval2),relvolume,price,dstr(fee),coin->symbol,coinaddr);
                        }
                        if ( (mini= LP_nearest_utxovalue(coin,coinaddr,utxos,m,(targetval2+2*fee) * 1.01)) >= 0 )
                        {
                            if ( up != 0 && (up2= utxos[mini]) != 0 )
                            {
                                LP_butxo_set(butxo,iambob,coin,up,up2,targetval);
                                return(butxo);
                            } else printf("cant find utxos[mini %d]\n",mini);
                        } else printf("cant find targetval2 %.8f\n",dstr(targetval2));
                    } else printf("failed ratio test %.8f\n",(double)up->U.value/targetval);
                } else if ( targetval != 0 && mini >= 0 )
                    printf("targetval %.8f mini.%d\n",dstr(targetval),mini);
                if ( targetval == 0 || mini < 0 )
                    break;
            }
        } else printf("no %s utxos pass LP_address_utxo_ptrs filter\n",coinaddr);
    } else printf("address_myutxopair couldnt find %s %s\n",coin->symbol,coinaddr);
    return(0);
}

int32_t LP_connectstartbob(void *ctx,int32_t pubsock,char *base,char *rel,double price,struct LP_quoteinfo *qp)
{
    char pairstr[512],otheraddr[64]; cJSON *reqjson; bits256 privkey; int32_t pair=-1,retval = -1,DEXselector = 0; struct basilisk_swap *swap; struct iguana_info *coin,*kmdcoin;
    qp->quotetime = (uint32_t)time(NULL);
    if ( (coin= LP_coinfind(qp->srccoin)) == 0 )
    {
        printf("cant find coin.%s\n",qp->srccoin);
        return(-1);
    }
    privkey = LP_privkey(coin->smartaddr,coin->taddr);
    if ( bits256_nonz(privkey) != 0 && bits256_cmp(G.LP_mypub25519,qp->srchash) == 0 )
    {
        LP_requestinit(&qp->R,qp->srchash,qp->desthash,base,qp->satoshis-qp->txfee,rel,qp->destsatoshis-qp->desttxfee,qp->timestamp,qp->quotetime,DEXselector);
        if ( LP_pendingswap(qp->R.requestid,qp->R.quoteid) > 0 )
        {
            printf("requestid.%u quoteid.%u is already in progres\n",qp->R.requestid,qp->R.quoteid);
            return(-1);
        }
        if ( (swap= LP_swapinit(1,0,privkey,&qp->R,qp,LP_dynamictrust(qp->othercredits,qp->desthash,LP_kmdvalue(qp->destcoin,qp->destsatoshis)) > 0)) == 0 )
        {
            printf("cant initialize swap\n");
            return(-1);
        }
        if ( (pair= LP_nanobind(ctx,pairstr)) >= 0 )
        {
            swap->N.pair = pair;
            //utxo->S.swap = swap;
            //swap->utxo = utxo;
            if ( OS_thread_create(malloc(sizeof(pthread_t)),NULL,(void *)LP_bobloop,(void *)swap) == 0 )
            {
                reqjson = LP_quotejson(qp);
                jaddstr(reqjson,"method","connected");
                jaddstr(reqjson,"pair",pairstr);
                if ( (kmdcoin= LP_coinfind("KMD")) != 0 )
                    jadd(reqjson,"proof",LP_instantdex_txids(0,kmdcoin->smartaddr));
                char str[65]; printf("BOB pubsock.%d binds to %d (%s)\n",pubsock,pair,bits256_str(str,qp->desthash));
                bits256 zero;
                memset(zero.bytes,0,sizeof(zero));
                LP_reserved_msg(1,base,rel,zero,jprint(reqjson,0));
                if ( IAMLP == 0 )
                {
                    sleep(1);
                    LP_reserved_msg(1,qp->srccoin,qp->destcoin,qp->desthash,jprint(reqjson,0));
                }
                LP_reserved_msg(0,base,rel,zero,jprint(reqjson,0));
                free_json(reqjson);
                LP_importaddress(qp->destcoin,qp->destaddr);
                LP_otheraddress(qp->srccoin,otheraddr,qp->destcoin,qp->destaddr);
                LP_importaddress(qp->srccoin,otheraddr);
                retval = 0;
            } else printf("error launching swaploop\n");
        } else printf("couldnt bind to any port %s\n",pairstr);
    }
    if ( retval < 0 )
    {
        if ( pair >= 0 )
            nn_close(pair);
        LP_availableset(qp->txid,qp->vout);
        LP_availableset(qp->txid2,qp->vout2);
    }
    return(retval);
}

char *LP_trade(void *ctx,char *myipaddr,int32_t mypubsock,struct LP_quoteinfo *qp,double maxprice,int32_t timeout,int32_t duration,uint32_t tradeid,bits256 destpubkey)
{
    double price;
    price = 0.;
    memset(qp->txid.bytes,0,sizeof(qp->txid));
    qp->txid2 = qp->txid;
    qp->aliceid = LP_aliceid_calc(qp->desttxid,qp->destvout,qp->feetxid,qp->feevout);
    if ( (qp->tradeid= tradeid) == 0 )
        qp->tradeid = LP_rand();
    LP_query(ctx,myipaddr,mypubsock,"request",qp);
    LP_Alicequery = *qp, LP_Alicemaxprice = maxprice, Alice_expiration = qp->timestamp + timeout, LP_Alicedestpubkey = destpubkey;
    char str[65]; printf("LP_trade %s/%s %.8f vol %.8f dest.(%s) maxprice %.8f\n",qp->srccoin,qp->destcoin,dstr(qp->satoshis),dstr(qp->destsatoshis),bits256_str(str,LP_Alicedestpubkey),maxprice);
    return(LP_recent_swaps(0));
}

int32_t LP_quotecmp(int32_t strictflag,struct LP_quoteinfo *qp,struct LP_quoteinfo *qp2)
{
    if ( bits256_nonz(LP_Alicedestpubkey) != 0 )
    {
        if (bits256_cmp(LP_Alicedestpubkey,qp->srchash) != 0 )
        {
            printf("reject quote from non-matching pubkey\n");
            return(-1);
        } else printf("dont reject quote from destpubkey\n");
    }
    if ( bits256_cmp(qp->desthash,qp2->desthash) == 0 && strcmp(qp->srccoin,qp2->srccoin) == 0 && strcmp(qp->destcoin,qp2->destcoin) == 0 && bits256_cmp(qp->desttxid,qp2->desttxid) == 0 && qp->destvout == qp2->destvout && bits256_cmp(qp->feetxid,qp2->feetxid) == 0 && qp->feevout == qp2->feevout && qp->destsatoshis == qp2->destsatoshis && qp->txfee >= qp2->txfee && qp->desttxfee == qp2->desttxfee )
    {
        if ( strictflag == 0 || (qp->aliceid == qp2->aliceid && qp->R.requestid == qp2->R.requestid && qp->R.quoteid == qp2->R.quoteid && qp->vout == qp2->vout && qp->vout2 == qp2->vout2 && qp->satoshis == qp2->satoshis && bits256_cmp(qp->txid,qp2->txid) == 0 && bits256_cmp(qp->txid2,qp2->txid2) == 0 && bits256_cmp(qp->srchash,qp2->srchash) == 0) )
            return(0);
        else printf("strict compare failure\n");
    }
    return(-1);
}

void LP_alicequery_clear()
{
    memset(&LP_Alicequery,0,sizeof(LP_Alicequery));
    memset(&LP_Alicedestpubkey,0,sizeof(LP_Alicedestpubkey));
    LP_Alicemaxprice = 0.;
    Alice_expiration = 0;
}

int32_t LP_alice_eligible(uint32_t quotetime)
{
    if ( Alice_expiration != 0 && quotetime > Alice_expiration )
    {
        printf("time expired for Alice_request\n");
        LP_alicequery_clear();
    }
    return(Alice_expiration == 0 || time(NULL) < Alice_expiration);
}

char *LP_connectedalice(struct LP_quoteinfo *qp,char *pairstr) // alice
{
    cJSON *retjson; char otheraddr[64]; double bid,ask,price,qprice; int32_t pairsock = -1; int32_t DEXselector = 0; struct LP_utxoinfo *autxo,A,B,*butxo; struct basilisk_swap *swap; struct iguana_info *coin;
    /*if ( LP_quoteparse(&Q,argjson) < 0 )
    {
        LP_aliceid(Q.tradeid,Q.aliceid,"error0",0,0);
        clonestr("{\"error\":\"cant parse quote\"}");
    }*/
    if ( bits256_cmp(qp->desthash,G.LP_mypub25519) != 0 )
    {
        LP_aliceid(qp->tradeid,qp->aliceid,"error1",0,0);
        return(clonestr("{\"result\",\"update stats\"}"));
    }
    printf("CONNECTED numpending.%d tradeid.%u requestid.%u quoteid.%u pairstr.%s\n",G.LP_pendingswaps,qp->tradeid,qp->R.requestid,qp->R.quoteid,pairstr!=0?pairstr:"");
    LP_requestinit(&qp->R,qp->srchash,qp->desthash,qp->srccoin,qp->satoshis-qp->txfee,qp->destcoin,qp->destsatoshis-qp->desttxfee,qp->timestamp,qp->quotetime,DEXselector);
    //printf("calculated requestid.%u quoteid.%u\n",qp->R.requestid,qp->R.quoteid);
    if ( LP_pendingswap(qp->R.requestid,qp->R.quoteid) > 0 )
    {
        printf("requestid.%u quoteid.%u is already in progres\n",qp->R.requestid,qp->R.quoteid);
        retjson = cJSON_CreateObject();
        jaddstr(retjson,"error","swap already in progress");
        return(jprint(retjson,1));
    }
    /*if ( LP_quotecmp(1,qp,&LP_Alicereserved) == 0 )
    {
        printf("mismatched between reserved and connected\n");
    }*/
    memset(&LP_Alicereserved,0,sizeof(LP_Alicereserved));
    LP_aliceid(qp->tradeid,qp->aliceid,"connected",qp->R.requestid,qp->R.quoteid);
    autxo = &A;
    butxo = &B;
    memset(autxo,0,sizeof(*autxo));
    memset(butxo,0,sizeof(*butxo));
    LP_abutxo_set(autxo,butxo,qp);
    if ( (qprice= LP_quote_validate(autxo,butxo,qp,0)) <= SMALLVAL )
    {
        LP_availableset(qp->desttxid,qp->vout);
        LP_availableset(qp->feetxid,qp->feevout);
        LP_aliceid(qp->tradeid,qp->aliceid,"error4",0,0);
        printf("quote %s/%s validate error %.0f\n",qp->srccoin,qp->destcoin,qprice);
        return(clonestr("{\"error\":\"quote validation error\"}"));
    }
    if ( LP_myprice(&bid,&ask,qp->srccoin,qp->destcoin) <= SMALLVAL || bid <= SMALLVAL )
    {
        printf("this node has no price for %s/%s (%.8f %.8f)\n",qp->destcoin,qp->srccoin,bid,ask);
        LP_availableset(qp->desttxid,qp->vout);
        LP_availableset(qp->feetxid,qp->feevout);
        LP_aliceid(qp->tradeid,qp->aliceid,"error5",0,0);
        return(clonestr("{\"error\":\"no price set\"}"));
    }
    //LP_RTmetrics_update(qp->srccoin,qp->destcoin);
    printf("%s/%s bid %.8f ask %.8f values %.8f %.8f\n",qp->srccoin,qp->destcoin,bid,ask,dstr(butxo->payment.value),dstr(butxo->deposit.value));
    price = bid;
    if ( (coin= LP_coinfind(qp->destcoin)) == 0 )
    {
        LP_aliceid(qp->tradeid,qp->aliceid,"error6",0,0);
        return(clonestr("{\"error\":\"cant get alicecoin\"}"));
    }
    qp->privkey = LP_privkey(qp->destaddr,coin->taddr);
    if ( bits256_nonz(qp->privkey) != 0 )//&& qp->quotetime >= qp->timestamp-3 )
    {
        retjson = cJSON_CreateObject();
        if ( (swap= LP_swapinit(0,0,qp->privkey,&qp->R,qp,LP_dynamictrust(qp->othercredits,qp->srchash,LP_kmdvalue(qp->srccoin,qp->satoshis)) > 0)) == 0 )
        {
            jaddstr(retjson,"error","couldnt swapinit");
            LP_availableset(qp->desttxid,qp->vout);
            LP_availableset(qp->feetxid,qp->feevout);
            LP_aliceid(qp->tradeid,qp->aliceid,"error7",qp->R.requestid,qp->R.quoteid);
            return(jprint(retjson,1));
        }
        if ( pairstr == 0 || pairstr[0] == 0 || (pairsock= nn_socket(AF_SP,NN_PAIR)) < 0 )
        {
            LP_aliceid(qp->tradeid,qp->aliceid,"error8",qp->R.requestid,qp->R.quoteid);
            jaddstr(retjson,"error","couldnt create pairsock");
        }
        else if ( nn_connect(pairsock,pairstr) >= 0 )
        {
            //timeout = 1;
            //nn_setsockopt(pairsock,NN_SOL_SOCKET,NN_SNDTIMEO,&timeout,sizeof(timeout));
            //nn_setsockopt(pairsock,NN_SOL_SOCKET,NN_RCVTIMEO,&timeout,sizeof(timeout));
            swap->tradeid = qp->tradeid;
            swap->N.pair = pairsock;
            //autxo->S.swap = swap;
            //swap->utxo = autxo;
            LP_importaddress(qp->srccoin,qp->coinaddr);
            LP_otheraddress(qp->destcoin,otheraddr,qp->srccoin,qp->coinaddr);
            LP_importaddress(qp->srccoin,otheraddr);
            LP_aliceid(qp->tradeid,qp->aliceid,"started",qp->R.requestid,qp->R.quoteid);
            printf("alice pairstr.(%s) pairsock.%d pthread_t %ld\n",pairstr,pairsock,sizeof(pthread_t));
            if ( OS_thread_create(malloc(sizeof(pthread_t)),NULL,(void *)LP_aliceloop,(void *)swap) == 0 )
            {
                retjson = LP_quotejson(qp);
                jaddstr(retjson,"result","success");
                //jaddnum(retjson,"requestid",qp->R.requestid);
                //jaddnum(retjson,"quoteid",qp->R.quoteid);
            }
            else
            {
                LP_aliceid(qp->tradeid,qp->aliceid,"error9",qp->R.requestid,qp->R.quoteid);
                jaddstr(retjson,"error","couldnt aliceloop");
            }
        }
        else
        {
            LP_aliceid(qp->tradeid,qp->aliceid,"error10",qp->R.requestid,qp->R.quoteid);
            printf("connect error %s\n",nn_strerror(nn_errno()));
        }
        printf("connected result.(%s)\n",jprint(retjson,0));
        if ( jobj(retjson,"error") != 0 )
        {
            LP_availableset(qp->desttxid,qp->vout);
            LP_availableset(qp->feetxid,qp->feevout);
        }
        return(jprint(retjson,1));
    }
    else
    {
        LP_availableset(qp->desttxid,qp->vout);
        LP_availableset(qp->feetxid,qp->feevout);
        LP_aliceid(qp->tradeid,qp->aliceid,"error11",0,0);
        printf("no privkey found coin.%s %s taddr.%u\n",qp->destcoin,qp->destaddr,coin->taddr);
        return(clonestr("{\"error\",\"no privkey\"}"));
    }
}

int32_t LP_aliceonly(char *symbol)
{
    if ( strcmp(symbol,"GAME") == 0 )
        return(1);
    else return(0);
}

int32_t LP_validSPV(char *symbol,char *coinaddr,bits256 txid,int32_t vout)
{
    struct electrum_info *ep,*backupep; cJSON *txobj; struct LP_address_utxo *up; struct iguana_info *coin; int32_t height; struct LP_transaction *tx;
    coin = LP_coinfind(symbol);
    if ( coin != 0 && (ep= coin->electrum) != 0 )
    {
        if ( (up= LP_address_utxofind(coin,coinaddr,txid,vout)) == 0 )
        {
            if ( (txobj= electrum_transaction(&height,symbol,ep,&txobj,txid,coinaddr)) != 0 )
                free_json(txobj);
            if ( (tx= LP_transactionfind(coin,txid)) != 0 )
            {
                if ( vout < tx->numvouts && tx->height > 0 )
                {
                    printf("added missing utxo for SPV checking\n");
                    LP_address_utxoadd((uint32_t)time(NULL),"LP_validSPV",coin,coinaddr,txid,vout,tx->outpoints[vout].value,tx->height,-1);
                }
            }
        }
        if ( (up= LP_address_utxofind(coin,coinaddr,txid,vout)) != 0 )
        {
            if ( up->SPV > 0 )
                return(0);
            if ( up->SPV < 0 )
                return(-1);
            if ( (backupep= ep->prev) == 0 )
                backupep = ep;
            up->SPV = LP_merkleproof(coin,coinaddr,backupep,up->U.txid,up->U.height);
            if ( up->SPV < 0 )
                return(-1);
        }
    }
    return(0);
}

double LP_trades_alicevalidate(void *ctx,struct LP_quoteinfo *qp)
{
    double qprice; struct LP_utxoinfo A,B,*autxo,*butxo; char str[65];
    autxo = &A;
    butxo = &B;
    memset(autxo,0,sizeof(*autxo));
    memset(butxo,0,sizeof(*butxo));
    LP_abutxo_set(autxo,butxo,qp);
    if ( (qprice= LP_quote_validate(autxo,butxo,qp,0)) <= SMALLVAL )
    {
        printf("reserved quote validate error %.0f\n",qprice);
        return((int32_t)qprice);
    }
    if ( LP_validSPV(qp->srccoin,qp->coinaddr,qp->txid,qp->vout) < 0 )
    {
        sleep(1);
        if ( LP_validSPV(qp->srccoin,qp->coinaddr,qp->txid,qp->vout) < 0 )
        {
            printf("LP_trades_alicevalidate %s src %s failed SPV check\n",qp->srccoin,bits256_str(str,qp->txid));
            return(-44);
        }
    }
    else if ( LP_validSPV(qp->srccoin,qp->coinaddr,qp->txid2,qp->vout2) < 0 )
    {
        sleep(1);
        if ( LP_validSPV(qp->srccoin,qp->coinaddr,qp->txid2,qp->vout2) < 0 )
        {
            printf("LP_trades_alicevalidate %s src2 %s failed SPV check\n",qp->srccoin,bits256_str(str,qp->txid2));
            return(-55);
        }
    }
    return(qprice);
}

void LP_reserved(void *ctx,char *myipaddr,int32_t mypubsock,struct LP_quoteinfo *qp)
{
    double price=0.,maxprice = LP_Alicemaxprice;
    //if ( LP_quotecmp(0,qp,&LP_Alicequery) == 0 )
    {
        price = LP_pricecache(qp,qp->srccoin,qp->destcoin,qp->txid,qp->vout);
        if ( LP_pricevalid(price) > 0 && maxprice > SMALLVAL && price <= maxprice )
        {
            qp->tradeid = LP_Alicequery.tradeid;
            LP_Alicereserved = *qp;
            LP_alicequery_clear();
            printf("send CONNECT\n");
            LP_query(ctx,myipaddr,mypubsock,"connect",qp);
        } else printf("LP_reserved %llu price %.8f vs maxprice %.8f\n",(long long)qp->aliceid,price,maxprice);
    } //else printf("probably a timeout, reject reserved due to not eligible.%d or mismatched quote price %.8f vs maxprice %.8f\n",LP_alice_eligible(qp->quotetime),price,maxprice);
}

double LP_trades_bobprice(double *bidp,double *askp,struct LP_quoteinfo *qp)
{
    double price; struct iguana_info *coin; char str[65];
    price = LP_myprice(bidp,askp,qp->srccoin,qp->destcoin);
    if ( (coin= LP_coinfind(qp->srccoin)) == 0 || price <= SMALLVAL || *askp <= SMALLVAL )
    {
        //printf("this node has no price for %s/%s\n",qp->srccoin,qp->destcoin);
        return(0.);
    }
    price = *askp;
    //printf("MYPRICE %s/%s %.8f vs qprice %.8f\n",qp->srccoin,qp->destcoin,price,(double)qp->destsatoshis/qp->satoshis);
    if ( LP_validSPV(qp->destcoin,qp->destaddr,qp->desttxid,qp->destvout) < 0 )
    {
        printf("LP_trades_bobprice %s dest %s failed SPV check\n",qp->destcoin,bits256_str(str,qp->desttxid));
        return(0.);
    }
    else if (LP_validSPV(qp->destcoin,qp->destaddr,qp->feetxid,qp->feevout) < 0 )
    {
        printf("LP_trades_bobprice %s dexfee %s failed SPV check\n",qp->destcoin,bits256_str(str,qp->feetxid));
        return(0.);
    }
    return(*askp);
}

double LP_trades_pricevalidate(struct LP_quoteinfo *qp,struct iguana_info *coin,double price)
{
    double qprice; struct LP_utxoinfo A,B,*autxo,*butxo;
    autxo = &A;
    butxo = &B;
    memset(autxo,0,sizeof(*autxo));
    memset(butxo,0,sizeof(*butxo));
    LP_abutxo_set(autxo,butxo,qp);
    if ( strcmp(qp->coinaddr,coin->smartaddr) != 0 )
    {
        printf("bob is patching qp->coinaddr %s mismatch != %s\n",qp->coinaddr,coin->smartaddr);
        strcpy(qp->coinaddr,coin->smartaddr);
    }
    if ( butxo == 0 || bits256_nonz(butxo->payment.txid) == 0 || bits256_nonz(butxo->deposit.txid) == 0 || butxo->payment.vout < 0 || butxo->deposit.vout < 0 )
    {
        char str[65],str2[65]; printf("couldnt find bob utxos for autxo %s/v%d %s/v%d %.8f -> %.8f\n",bits256_str(str,qp->txid),qp->vout,bits256_str(str2,qp->txid2),qp->vout2,dstr(qp->satoshis),dstr(qp->destsatoshis));
        return(-66);
    }
    if ( (qprice= LP_quote_validate(autxo,butxo,qp,1)) <= SMALLVAL )
    {
        printf("quote %s/%s validate error %.0f\n",qp->srccoin,qp->destcoin,qprice);
        return(-3);
    }
    if ( qprice < (price - 0.00000001) * 0.998 )
    {
        printf(" quote price %.8f too low vs %.8f for %s/%s %.8f < %.8f\n",qprice,price,qp->srccoin,qp->destcoin,qprice,(price - 0.00000001) * 0.998);
        return(-77);
    }
    return(qprice);
}

struct LP_quoteinfo *LP_trades_gotrequest(void *ctx,struct LP_quoteinfo *qp,struct LP_quoteinfo *newqp,char *pairstr)
{
    double price,qprice,myprice,bestprice,range,bid,ask; struct iguana_info *coin; struct LP_utxoinfo A,B,*autxo,*butxo; cJSON *reqjson; char str[65]; struct LP_address_utxo *utxos[1000]; int32_t r,counter,max = (int32_t)(sizeof(utxos)/sizeof(*utxos));
    *newqp = *qp;
    qp = newqp;
    if ( (coin= LP_coinfind(qp->srccoin)) == 0 )
        return(0);
    //printf("LP_trades_gotrequest %s/%s %.8f\n",qp->srccoin,qp->destcoin,LP_trades_bobprice(&bid,&ask,qp));
    if ( (myprice= LP_trades_bobprice(&bid,&ask,qp)) == 0. )
        return(0);
    autxo = &A;
    butxo = &B;
    memset(autxo,0,sizeof(*autxo));
    memset(butxo,0,sizeof(*butxo));
    LP_abutxo_set(autxo,butxo,qp);
    if ( bits256_nonz(qp->srchash) == 0 || bits256_cmp(qp->srchash,G.LP_mypub25519) == 0 )
    {
        qprice = (double)qp->destsatoshis / (qp->satoshis - qp->txfee);
        strcpy(qp->gui,G.gui);
        strcpy(qp->coinaddr,coin->smartaddr);
        strcpy(butxo->coinaddr,coin->smartaddr);
        qp->srchash = G.LP_mypub25519;
        memset(&qp->txid,0,sizeof(qp->txid));
        memset(&qp->txid2,0,sizeof(qp->txid2));
        qp->vout = qp->vout2 = -1;
    } else return(0);
    //printf("LP_trades_gotrequest qprice %.8f vs myprice %.8f\n",qprice,myprice);
    if ( qprice > myprice )
    {
        r = (LP_rand() % 100);
        range = (qprice - myprice);
        price = myprice + (r * range) / 100.;
        bestprice = LP_bob_competition(&counter,qp->aliceid,price,0);
        printf("%llu >>>>>>> qprice %.8f r.%d range %.8f -> %.8f, bestprice %.8f counter.%d\n",(long long)qp->aliceid,qprice,r,range,price,bestprice,counter);
        if ( counter > 3 && price >= bestprice+SMALLVAL ) // skip if late or bad price
            return(0);
    } else return(0);
    //LP_RTmetrics_update(qp->srccoin,qp->destcoin);
    if ( LP_RTmetrics_blacklisted(qp->desthash) >= 0 )
    {
        printf("request from blacklisted %s, ignore\n",bits256_str(str,qp->desthash));
        return(0);
    }
    LP_address_utxo_reset(coin);
    if ( (butxo= LP_address_myutxopair(butxo,1,utxos,max,LP_coinfind(qp->srccoin),qp->coinaddr,qp->txfee,dstr(qp->destsatoshis),price,qp->desttxfee)) != 0 )
    {
        strcpy(qp->gui,G.gui);
        strcpy(qp->coinaddr,coin->smartaddr);
        qp->srchash = G.LP_mypub25519;
        qp->txid = butxo->payment.txid;
        qp->vout = butxo->payment.vout;
        qp->txid2 = butxo->deposit.txid;
        qp->vout2 = butxo->deposit.vout;
        qp->satoshis = butxo->swap_satoshis;// + qp->txfee;
        qp->quotetime = (uint32_t)time(NULL);
    }
    else
    {
        printf("cant find utxopair aliceid.%llu %s/%s %.8f -> relvol %.8f\n",(long long)qp->aliceid,qp->srccoin,qp->destcoin,dstr(LP_basesatoshis(dstr(qp->destsatoshis),price,qp->txfee,qp->desttxfee)),dstr(qp->destsatoshis));
        return(0);
    }
    if ( (qprice= LP_trades_pricevalidate(qp,coin,myprice)) < 0. )
        return(0);
    if ( LP_allocated(qp->txid,qp->vout) == 0 && LP_allocated(qp->txid2,qp->vout2) == 0 )
    {
        reqjson = LP_quotejson(qp);
        LP_unavailableset(qp->txid,qp->vout,qp->timestamp + LP_RESERVETIME,qp->desthash);
        LP_unavailableset(qp->txid2,qp->vout2,qp->timestamp + LP_RESERVETIME,qp->desthash);
        if ( qp->quotetime == 0 )
            qp->quotetime = (uint32_t)time(NULL);
        jaddnum(reqjson,"quotetime",qp->quotetime);
        jaddnum(reqjson,"pending",qp->timestamp + LP_RESERVETIME);
        jaddstr(reqjson,"method","reserved");
        bits256 zero;
        memset(zero.bytes,0,sizeof(zero));
        LP_reserved_msg(1,qp->srccoin,qp->destcoin,zero,jprint(reqjson,0));
        if ( IAMLP == 0 )
        {
            sleep(1);
            LP_reserved_msg(1,qp->srccoin,qp->destcoin,qp->desthash,jprint(reqjson,0));
        }
        LP_reserved_msg(0,qp->srccoin,qp->destcoin,zero,jprint(reqjson,0));
        free_json(reqjson);
        return(qp);
    } else printf("request processing selected ineligible utxos?\n");
    return(0);
}

struct LP_quoteinfo *LP_trades_gotreserved(void *ctx,struct LP_quoteinfo *qp,struct LP_quoteinfo *newqp)
{
    char *retstr;
    char str[65]; printf("alice %s received RESERVED.(%llu) %.8f\n",bits256_str(str,G.LP_mypub25519),(long long)qp->aliceid,(double)qp->destsatoshis/(qp->satoshis+1));
    *newqp = *qp;
    qp = newqp;
    if ( LP_trades_alicevalidate(ctx,qp) > 0. )
    {
        LP_aliceid(qp->tradeid,qp->aliceid,"reserved",0,0);
        if ( (retstr= LP_quotereceived(qp)) != 0 )
            free(retstr);
        return(qp);
    }
    return(0);
}

struct LP_quoteinfo *LP_trades_gotconnect(void *ctx,struct LP_quoteinfo *qp,struct LP_quoteinfo *newqp,char *pairstr)
{
    double myprice,qprice,bid,ask; struct iguana_info *coin;
    *newqp = *qp;
    qp = newqp;
    if ( (coin= LP_coinfind(qp->srccoin)) == 0 )
       return(0);
    if ( (myprice= LP_trades_bobprice(&bid,&ask,qp)) == 0. )
        return(0);
    if ( (qprice= LP_trades_pricevalidate(qp,coin,myprice)) < 0. )
        return(0);
    if ( LP_reservation_check(qp->txid,qp->vout,qp->desthash) == 0 && LP_reservation_check(qp->txid2,qp->vout2,qp->desthash) == 0  )
    {
        LP_connectstartbob(ctx,LP_mypubsock,qp->srccoin,qp->destcoin,qprice,qp);
        return(qp);
    } else printf("connect message from non-reserved (%llu)\n",(long long)qp->aliceid);
    return(0);
}

struct LP_quoteinfo *LP_trades_gotconnected(void *ctx,struct LP_quoteinfo *qp,struct LP_quoteinfo *newqp,char *pairstr)
{
    char *retstr;
    char str[65]; printf("alice %s received CONNECTED.(%llu)\n",bits256_str(str,G.LP_mypub25519),(long long)qp->aliceid);
    *newqp = *qp;
    qp = newqp;
    if ( LP_trades_alicevalidate(ctx,qp) > 0. )
    {
        //printf("LP_trades_alicevalidate fine\n");
        LP_aliceid(qp->tradeid,qp->aliceid,"connected",0,0);
        if ( (retstr= LP_connectedalice(qp,pairstr)) != 0 )
            free(retstr);
        return(qp);
    }
    //printf("LP_trades_alicevalidate error\n");
    return(0);
}

int32_t LP_trades_bestpricecheck(void *ctx,struct LP_trade *tp)
{
    double qprice; int32_t flag = 0; struct LP_quoteinfo Q; int64_t dynamictrust; char *retstr; struct LP_pubkey_info *pubp;
    Q = tp->Q;
    //printf("check bestprice %.8f vs new price %.8f\n",tp->bestprice,(double)Q.destsatoshis/Q.satoshis);
    if ( Q.satoshis != 0 && (pubp= LP_pubkeyadd(Q.srchash)) != 0 )//(qprice= LP_trades_alicevalidate(ctx,&Q)) > 0. )
    {
        qprice = (double)Q.destsatoshis / (Q.satoshis - Q.txfee);
        LP_aliceid(Q.tradeid,tp->aliceid,"reserved",0,0);
        if ( (retstr= LP_quotereceived(&Q)) != 0 )
            free(retstr);
        //LP_trades_gotreserved(ctx,&Q,&tp->Qs[LP_RESERVED]);
        dynamictrust = LP_dynamictrust(Q.othercredits,Q.srchash,LP_kmdvalue(Q.srccoin,Q.satoshis));
        if ( tp->bestprice == 0. )
            flag = 1;
        else if ( qprice < tp->bestprice && pubp->slowresponse <= tp->bestresponse*1.05 )
            flag = 1;
        else if ( qprice < tp->bestprice*1.01 && dynamictrust > tp->besttrust && pubp->slowresponse <= tp->bestresponse*1.1 )
            flag = 1;
        else if ( qprice <= tp->bestprice && pubp->unconfcredits > tp->bestunconfcredits && pubp->slowresponse <= tp->bestresponse )
            flag = 1;
        if ( flag != 0 )
        {
            tp->Qs[LP_CONNECT] = tp->Q;
            tp->bestprice = qprice;
            tp->besttrust = dynamictrust;
            tp->bestunconfcredits = pubp->unconfcredits;
            tp->bestresponse = pubp->slowresponse;
            printf("aliceid.%llu got new bestprice %.8f dynamictrust %.8f (unconf %.8f) slowresponse.%d\n",(long long)tp->aliceid,tp->bestprice,dstr(dynamictrust),dstr(tp->bestunconfcredits),tp->bestresponse);
            return(qprice);
        } //else printf("qprice %.8f dynamictrust %.8f not good enough\n",qprice,dstr(dynamictrust));
    } else printf("alice didnt validate\n");
    return(0);
}

void LP_tradesloop(void *ctx)
{
    struct LP_trade *qtp,*tp,*tmp; struct LP_quoteinfo *qp,Q; uint32_t now; int32_t timeout,funcid,flag,nonz; struct iguana_info *coin; struct LP_pubkey_info *pubp;
    strcpy(LP_tradesloop_stats.name,"LP_tradesloop");
    LP_tradesloop_stats.threshold = 10000;
    sleep(5);
    while ( 1 )
    {
        LP_millistats_update(&LP_tradesloop_stats);
        nonz = 0;
        DL_FOREACH_SAFE(LP_tradesQ,qtp,tmp)
        {
            now = (uint32_t)time(NULL);
            Q = qtp->Q;
            funcid = qtp->funcid;
//printf("dequeue %p funcid.%d aliceid.%llu iambob.%d\n",qtp,funcid,(long long)qtp->aliceid,qtp->iambob);
            portable_mutex_lock(&LP_tradesmutex);
            DL_DELETE(LP_tradesQ,qtp);
            HASH_FIND(hh,LP_trades,&qtp->aliceid,sizeof(qtp->aliceid),tp);
            if ( tp == 0 )
            {
                tp = qtp;
                HASH_ADD(hh,LP_trades,aliceid,sizeof(tp->aliceid),tp);
                portable_mutex_unlock(&LP_tradesmutex);
                if ( tp->iambob != 0 && funcid == LP_REQUEST ) // bob maybe sends LP_RESERVED
                {
                    if ( (qp= LP_trades_gotrequest(ctx,&Q,&tp->Qs[LP_REQUEST],tp->pairstr)) != 0 )
                        tp->Qs[LP_RESERVED] = Q;
                }
                else if ( tp->iambob == 0 && funcid == LP_RESERVED ) // alice maybe sends LP_CONNECT
                {
                    LP_trades_bestpricecheck(ctx,tp);
                }
                nonz++;
                tp->firstprocessed = tp->lastprocessed = (uint32_t)time(NULL);
//printf("iambob.%d funcid.%d vs %d\n",tp->iambob,funcid,LP_REQUEST);
                continue;
            }
            portable_mutex_unlock(&LP_tradesmutex);
            tp->Q = qtp->Q;
            if ( qtp->iambob == tp->iambob && qtp->pairstr[0] != 0 )
                safecopy(tp->pairstr,qtp->pairstr,sizeof(tp->pairstr));
//printf("finished dequeue %p funcid.%d aliceid.%llu iambob.%d\n",qtp,funcid,(long long)qtp->aliceid,qtp->iambob);
            free(qtp);
            if ( tp->negotiationdone != 0 )
                continue;
            flag = 0;
            if ( qtp->iambob == tp->iambob )
            {
                if ( tp->iambob == 0 )
                {
                    if ( funcid == LP_RESERVED )
                    {
                        if ( tp->connectsent == 0 )
                            flag = LP_trades_bestpricecheck(ctx,tp);
                    }
                    else if ( funcid == LP_CONNECTED && tp->connectsent != 0 && tp->negotiationdone == 0 ) // alice all done
                    {
                        flag = 1;
                        tp->negotiationdone = now;
                        LP_trades_gotconnected(ctx,&tp->Q,&tp->Qs[LP_CONNECTED],tp->pairstr);
                    }
                }
                else
                {
                    if ( funcid == LP_CONNECT && tp->negotiationdone == 0 ) // bob all cone
                    {
                        flag = 1;
                        tp->negotiationdone = now;
                        LP_trades_gotconnect(ctx,&tp->Q,&tp->Qs[LP_CONNECT],tp->pairstr);
                    }
                }
                if ( flag != 0 )
                {
                    tp->lastprocessed = (uint32_t)time(NULL);
                    nonz++;
                }
            }
        }
        HASH_ITER(hh,LP_trades,tp,tmp)
        {
            if ( tp->negotiationdone != 0 )
                continue;
            timeout = LP_AUTOTRADE_TIMEOUT;
            if ( (coin= LP_coinfind(tp->Q.srccoin)) != 0 && coin->electrum != 0 )
                timeout += LP_AUTOTRADE_TIMEOUT * .5;
            if ( (coin= LP_coinfind(tp->Q.destcoin)) != 0 && coin->electrum != 0 )
                timeout += LP_AUTOTRADE_TIMEOUT * .5;
            now = (uint32_t)time(NULL);
            if ( now > tp->lastprocessed )
            {
                if ( tp->iambob == 0 )
                {
                    if ( tp->bestprice > 0. )
                    {
                        if ( tp->connectsent == 0 )
                        {
                            LP_Alicemaxprice = tp->bestprice;
                            LP_reserved(ctx,LP_myipaddr,LP_mypubsock,&tp->Qs[LP_CONNECT]); // send LP_CONNECT
                            tp->connectsent = now;
                            //printf("send LP_connect aliceid.%llu %.8f\n",(long long)tp->aliceid,tp->bestprice);
                        }
                        else if ( now < tp->firstprocessed+timeout && ((tp->firstprocessed - now) % 10) == 9 )
                        {
                            LP_Alicemaxprice = tp->bestprice;
                            LP_reserved(ctx,LP_myipaddr,LP_mypubsock,&tp->Qs[LP_CONNECT]); // send LP_CONNECT
                            printf("repeat LP_connect aliceid.%llu %.8f\n",(long long)tp->aliceid,tp->bestprice);
                            if ( (pubp= LP_pubkeyfind(tp->Qs[LP_CONNECT].srchash)) != 0 )
                                pubp->slowresponse++;
                        }
                    }
                }
                else if ( now > tp->firstprocessed+timeout*10 )
                {
                    //printf("purge swap aliceid.%llu\n",(long long)tp->aliceid);
                    portable_mutex_lock(&LP_tradesmutex);
                    HASH_DELETE(hh,LP_trades,tp);
                    portable_mutex_unlock(&LP_tradesmutex);
                    free(tp);
                }
            }
        }
        if ( nonz == 0 )
            sleep(1);
    }
}

void LP_tradecommandQ(struct LP_quoteinfo *qp,char *pairstr,int32_t funcid)
{
    struct LP_trade *qtp; uint64_t aliceid; int32_t iambob;
    if ( funcid < 0 || funcid >= sizeof(qtp->Qs)/sizeof(*qtp->Qs) )
        return;
    if ( funcid == LP_REQUEST || funcid == LP_CONNECT )
        iambob = 1;
    else iambob = 0;
    aliceid = qp->aliceid;
    portable_mutex_lock(&LP_tradesmutex);
    qtp = calloc(1,sizeof(*qtp));
    qtp->funcid = funcid;
    qtp->iambob = iambob;
    qtp->aliceid = aliceid;
    qtp->newtime = (uint32_t)time(NULL);
    qtp->Q = *qp;
    if ( pairstr != 0 )
        safecopy(qtp->pairstr,pairstr,sizeof(qtp->pairstr));
    DL_APPEND(LP_tradesQ,qtp);
    portable_mutex_unlock(&LP_tradesmutex);
    //printf("queue.%d %p\n",funcid,qtp);
}

int32_t LP_tradecommand(void *ctx,char *myipaddr,int32_t pubsock,cJSON *argjson,uint8_t *data,int32_t datalen)
{
    int32_t Qtrades = 1;
    char *method,str[65]; int32_t num,DEXselector = 0; uint64_t aliceid; double qprice,bestprice,price,bid,ask; cJSON *proof; struct iguana_info *coin; struct LP_quoteinfo Q,Q2; int32_t counter,retval=-1;
    if ( (method= jstr(argjson,"method")) != 0 && (strcmp(method,"reserved") == 0 ||strcmp(method,"connected") == 0 || strcmp(method,"request") == 0 || strcmp(method,"connect") == 0) )
    {
        LP_quoteparse(&Q,argjson);
        LP_requestinit(&Q.R,Q.srchash,Q.desthash,Q.srccoin,Q.satoshis-Q.txfee,Q.destcoin,Q.destsatoshis-Q.desttxfee,Q.timestamp,Q.quotetime,DEXselector);
        LP_tradecommand_log(argjson);
        printf("%-4d (%-10u %10u) %12s id.%22llu %5s/%-5s %12.8f -> %11.8f price %11.8f | RT.%d %d\n",(uint32_t)time(NULL) % 3600,Q.R.requestid,Q.R.quoteid,method,(long long)Q.aliceid,Q.srccoin,Q.destcoin,dstr(Q.satoshis),dstr(Q.destsatoshis),(double)Q.destsatoshis/Q.satoshis,LP_RTcount,LP_swapscount);
        LP_autoprices_update(method,Q.srccoin,dstr(Q.satoshis),Q.destcoin,dstr(Q.destsatoshis));
        retval = 1;
        aliceid = j64bits(argjson,"aliceid");
        qprice = jdouble(argjson,"price");
        if ( strcmp(method,"reserved") == 0 )
        {
            bestprice = LP_bob_competition(&counter,aliceid,qprice,1);
            //printf("%s lag %ld: aliceid.%llu price %.8f -> bestprice %.8f Alice max %.8f\n",jprint(argjson,0),Q.quotetime - (time(NULL)-20),(long long)aliceid,qprice,bestprice,LP_Alicemaxprice);
            if ( 0 )
            {
                if ( LP_Alicemaxprice == 0. )
                    return(retval);
                if ( bits256_nonz(LP_Alicedestpubkey) != 0 )
                {
                    if (bits256_cmp(LP_Alicedestpubkey,Q.srchash) != 0 )
                    {
                        printf("got reserved response from different node %s\n",bits256_str(str,Q.srchash));
                        return(retval);
                    } else printf("got reserved response from destpubkey %s\n",bits256_str(str,Q.srchash));
                }
            }
            if ( bits256_cmp(G.LP_mypub25519,Q.desthash) == 0 && bits256_cmp(G.LP_mypub25519,Q.srchash) != 0 )
            {
                if ( Qtrades == 0 )
                {
                    if ( Q.quotetime > time(NULL)-20 && LP_alice_eligible(Q.quotetime) > 0 )
                    {
                        LP_trades_gotreserved(ctx,&Q,&Q2);
                        if ( LP_quotecmp(0,&Q,&LP_Alicequery) == 0 )
                            LP_reserved(ctx,LP_myipaddr,LP_mypubsock,&Q);
                    }
                } else LP_tradecommandQ(&Q,jstr(argjson,"pair"),LP_RESERVED);
            }
        }
        else if ( strcmp(method,"connected") == 0 )
        {
            bestprice = LP_bob_competition(&counter,aliceid,qprice,1000);
            if ( bits256_cmp(G.LP_mypub25519,Q.desthash) == 0 && bits256_cmp(G.LP_mypub25519,Q.srchash) != 0 )
            {
                //printf("CONNECTED.(%s)\n",jprint(argjson,0));
                if ( (proof= jarray(&num,argjson,"proof")) != 0 && num > 0 )
                    Q.othercredits = LP_instantdex_proofcheck(Q.coinaddr,proof,num);
                if ( Qtrades == 0 )
                    LP_trades_gotconnected(ctx,&Q,&Q2,jstr(argjson,"pair"));
                else LP_tradecommandQ(&Q,jstr(argjson,"pair"),LP_CONNECTED);
            }
        }
        price = LP_myprice(&bid,&ask,Q.srccoin,Q.destcoin);
        if ( (coin= LP_coinfind(Q.srccoin)) == 0 || coin->inactive != 0 )
        {
            //printf("%s is not active\n",Q.srccoin);
            return(retval);
        }
        if ( price <= SMALLVAL || ask <= SMALLVAL )
        {
            //printf("this node has no price for %s/%s\n",Q.srccoin,Q.destcoin);
            return(retval);
        }
        if ( LP_aliceonly(Q.srccoin) > 0 )
        {
            printf("{\"error\":\"GAME can only be alice coin\"}\n");
            return(retval);
        }
        if ( strcmp(method,"request") == 0 )
        {
            bestprice = LP_bob_competition(&counter,aliceid,qprice,-1);
            if ( Qtrades == 0 )
                LP_trades_gotrequest(ctx,&Q,&Q2,jstr(argjson,"pair"));
            else LP_tradecommandQ(&Q,jstr(argjson,"pair"),LP_REQUEST);
        }
        else if ( strcmp(method,"connect") == 0 )
        {
            LP_bob_competition(&counter,aliceid,qprice,1000);
            if ( bits256_cmp(G.LP_mypub25519,Q.srchash) == 0 && bits256_cmp(G.LP_mypub25519,Q.desthash) != 0 )
            {
                printf("CONNECT.(%s)\n",jprint(argjson,0));
                if ( (proof= jarray(&num,argjson,"proof")) != 0 && num > 0 )
                    Q.othercredits = LP_instantdex_proofcheck(Q.destaddr,proof,num);
                if ( Qtrades == 0 )
                    LP_trades_gotconnect(ctx,&Q,&Q2,jstr(argjson,"pair"));
                else LP_tradecommandQ(&Q,jstr(argjson,"pair"),LP_CONNECT);
            }
        }
        return(retval);
    }
    return(retval);
}

char *LP_autobuy(void *ctx,char *myipaddr,int32_t mypubsock,char *base,char *rel,double maxprice,double relvolume,int32_t timeout,int32_t duration,char *gui,uint32_t nonce,bits256 destpubkey,uint32_t tradeid)
{
    uint64_t desttxfee,txfee; uint32_t lastnonce; int64_t bestsatoshis=0,destsatoshis; struct iguana_info *basecoin,*relcoin; struct LP_utxoinfo *autxo,B,A; struct LP_quoteinfo Q; bits256 pubkeys[100]; struct LP_address_utxo *utxos[1000]; int32_t max=(int32_t)(sizeof(utxos)/sizeof(*utxos));
    basecoin = LP_coinfind(base);
    relcoin = LP_coinfind(rel);
    if ( gui == 0 )
        gui = "nogui";
    if ( basecoin == 0 || basecoin->inactive != 0 || relcoin == 0 || relcoin->inactive != 0 )
        return(clonestr("{\"error\":\"base or rel not found or inactive\"}"));
    if ( LP_aliceonly(base) > 0 )
        return(clonestr("{\"error\":\"GAME can only be alice coin\"}"));
    printf("LP_autobuy %s/%s price %.8f vol %.8f nonce %u\n",base,rel,maxprice,relvolume,nonce);
    if ( (lastnonce= LP_lastnonce) != 0 && nonce <= lastnonce )
    {
        printf("nonce.%u not bigger than lastnonce.%u\n",nonce,lastnonce);
        return(clonestr("{\"error\":\"invalid nonce\"}"));
    }
    LP_lastnonce = nonce;
    if ( duration <= 0 )
        duration = LP_ORDERBOOK_DURATION;
    if ( timeout <= 0 )
        timeout = LP_AUTOTRADE_TIMEOUT;
    if ( basecoin->electrum != 0 && relcoin->electrum != 0 )
    {
        if ( timeout < 2*LP_AUTOTRADE_TIMEOUT )
            timeout = 2*LP_AUTOTRADE_TIMEOUT;
    }
    else if ( basecoin->electrum != 0 || relcoin->electrum != 0 )
    {
        if ( timeout < 1.5*LP_AUTOTRADE_TIMEOUT )
            timeout = 1.5*LP_AUTOTRADE_TIMEOUT;
    }
    if ( time(NULL) < Alice_expiration )
    {
        cJSON *retjson = cJSON_CreateObject();
        jaddstr(retjson,"error","only one pending request at a time");
        jaddnum(retjson,"wait",Alice_expiration-time(NULL));
        return(jprint(retjson,1));
    } else LP_alicequery_clear();
    if ( maxprice <= 0. || relvolume <= 0. || LP_priceinfofind(base) == 0 || LP_priceinfofind(rel) == 0 )
        return(clonestr("{\"error\":\"invalid parameter\"}"));
    if ( strcmp("BTC",rel) == 0 )
        maxprice *= 1.01;
    else maxprice *= 1.001;
    memset(pubkeys,0,sizeof(pubkeys));
    LP_txfees(&txfee,&desttxfee,base,rel);
    destsatoshis = SATOSHIDEN * relvolume + 2*desttxfee;
    memset(&A,0,sizeof(A));
    LP_address_utxo_reset(relcoin);
    if ( (autxo= LP_address_myutxopair(&A,0,utxos,max,relcoin,relcoin->smartaddr,txfee,dstr(destsatoshis),maxprice,desttxfee)) == 0 )
        return(clonestr("{\"error\":\"cant find a deposit that is close enough in size. make another deposit that is just a bit larger than what you want to trade\"}"));
    //printf("bestfit selected alice (%.8f %.8f) for %.8f sats %.8f\n",dstr(autxo->payment.value),dstr(autxo->fee.value),dstr(destsatoshis),dstr(autxo->swap_satoshis));
    if ( destsatoshis - desttxfee < autxo->swap_satoshis )
    {
        destsatoshis -= desttxfee;
        autxo->swap_satoshis = destsatoshis;
        //printf("first path dest %.8f from %.8f\n",dstr(destsatoshis),dstr(autxo->swap_satoshis));
    }
    else if ( autxo->swap_satoshis - desttxfee < destsatoshis )
    {
        autxo->swap_satoshis -= desttxfee;
        destsatoshis = autxo->swap_satoshis;
        printf("second path dest %.8f from %.8f\n",dstr(destsatoshis),dstr(autxo->swap_satoshis));
    }
    if ( destsatoshis < (autxo->payment.value / LP_MINCLIENTVOL) || autxo->payment.value < desttxfee*LP_MINSIZE_TXFEEMULT )
    {
        printf("destsatoshis %.8f vs utxo %.8f this would have triggered an quote error -13\n",dstr(destsatoshis),dstr(autxo->payment.value));
        return(clonestr("{\"error\":\"cant find a deposit that is close enough in size. make another deposit that is just a bit larger than what you want to trade\"}"));
    }
    bestsatoshis = 1.001 * LP_basesatoshis(dstr(destsatoshis),maxprice,txfee,desttxfee);
    memset(&B,0,sizeof(B));
    strcpy(B.coin,base);
    if ( LP_quoteinfoinit(&Q,&B,rel,maxprice,bestsatoshis,destsatoshis) < 0 )
        return(clonestr("{\"error\":\"cant set ordermatch quote\"}"));
    if ( LP_quotedestinfo(&Q,autxo->payment.txid,autxo->payment.vout,autxo->fee.txid,autxo->fee.vout,G.LP_mypub25519,autxo->coinaddr) < 0 )
        return(clonestr("{\"error\":\"cant set ordermatch quote info\"}"));
    int32_t changed;
    LP_mypriceset(&changed,autxo->coin,base,1. / maxprice);
    LP_mypriceset(&changed,base,autxo->coin,0.);
    return(LP_trade(ctx,myipaddr,mypubsock,&Q,maxprice,timeout,duration,tradeid,destpubkey));
}


