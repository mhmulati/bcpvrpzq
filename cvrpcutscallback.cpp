#include "cvrpcutscallback.h"
#include "CVRPSEP/include/capsep.h"
#include "CVRPSEP/include/mstarsep.h"
#include "CVRPSEP/include/fcisep.h"
#include "CVRPSEP/include/combsep.h"
#include <lemon/list_graph.h>

CVRPCutsCallback::CVRPCutsCallback(const CVRPInstance &cvrp, EdgeGRBVarMap& x) : cvrp(cvrp),x(x)  {    }

void CVRPCutsCallback::initializeCVRPSEPConstants(const CVRPInstance &cvrp){
    NoOfCustomers = cvrp.n - 1;
    CAP = cvrp.capacity;
    EpsForIntegrality = 0.0001;
    MaxNoOfCapCuts = 100;
    MaxNoOfFCITreeNodes = 100;
    MaxNoOfFCICuts = 100;
    MaxNoOfMStarCuts = 100;
    MaxNoOfCombCuts = 100;

    //initialize Constraint structure
    CMGR_CreateCMgr(&MyCutsCMP,100);
    CMGR_CreateCMgr(&MyOldCutsCMP,100);

    //populate Demand vector
    int demandSum = 0;
    Demand = new int[NoOfCustomers + 1];
    for(NodeIt v(cvrp.g); v != INVALID; ++v){
        if(cvrp.vname[v] != 0){
            Demand[cvrp.vname[v]] = int(cvrp.demand[v]);
            demandSum += int(cvrp.demand[v]);
        }
    }

    //QMin = sum_i(d_i) - (K - 1)Q
    QMin = demandSum - (cvrp.nroutes - 1) * cvrp.capacity;
}

void CVRPCutsCallback::freeDemand(){
    CMGR_FreeMemCMgr(&MyCutsCMP);
    CMGR_FreeMemCMgr(&MyOldCutsCMP);
    delete[] Demand;
}

//return the expression for x(delta(S))
GRBLinExpr CVRPCutsCallback::getDeltaExpr(int *S, int size){
    bool set[cvrp.n];
    GRBLinExpr expr = 0;

    //replace depot number from N to 0
    for(int i = 1; i < size; i++){
        if(S[i] == cvrp.n){
            S[i] = 0;
            break;
        }
    }

    //create a set for fast checking
    fill_n(set, cvrp.n, false);
    for(int i = 1; i < size; i++){
        set[S[i]] = true;
    }

    //get the expression
    //cout << "Delta: ";
    for(int i = 0; i < cvrp.n; i++){
        if(!set[i]){
            for(int j = 1; j < size; j++){
                Node u = cvrp.g.nodeFromId(i);
                Node v = cvrp.g.nodeFromId(S[j]);
                Edge e = findEdge(cvrp.g,u,v);
                expr += x[e];
                //cout << " + x[" << i << "][" << S[j] << "]";
            }
        }
    }
    //cout << endl;

    return expr;
}

//return the expression for x(S1:S2)
GRBLinExpr CVRPCutsCallback::getCrossingExpr(int *S1, int *S2, int size1, int size2){
    GRBLinExpr expr = 0;

    //replace depot number from N to 0
    /*
    for(int i = 1; i < size1; i++){
        if(S1[i] == cvrp.n){
            S1[i] = 0;
            break;
        }
    }

    for(int i = 1; i < size2; i++){
        if(S2[i] == cvrp.n){
            S2[i] = 0;
            break;
        }
    }*/

    //cout << "Crossing: ";
    //get the expression
    for(int i = 1; i < size1; i++){
        for(int j = 1; j < size2; j++){
            if(S1[i] != S2[j]){
                Node u = cvrp.g.nodeFromId(S1[i]);
                Node v = cvrp.g.nodeFromId(S2[j]);
                Edge e = findEdge(cvrp.g,u,v);
                expr += x[e];
            }
            //cout << " + x[" << S1[i] << "][" << S2[j] << "]";
        }
    }
    //cout << endl;

    return expr;
}

//return the expression for x(S:S)
GRBLinExpr CVRPCutsCallback::getInsideExpr(int *S, int size){
    GRBLinExpr expr = 0;

    //replace depot number from N to 0
    /*
    for(int i = 1; i < size; i++){
        if(S[i] == cvrp.n){
            S[i] = 0;
            break;
        }
    }*/

    //cout << "Crossing: ";
    //get the expression
    for(int i = 1; i < size; i++){
        for(int j = i + 1; j < size; j++){
            Node u = cvrp.g.nodeFromId(S[i]);
            Node v = cvrp.g.nodeFromId(S[j]);
            Edge e = findEdge(cvrp.g,u,v);
            expr += x[e];
            //cout << " + x[" << S1[i] << "][" << S2[j] << "]";
        }
    }
    //cout << endl;

    return expr;
}

void CVRPCutsCallback::callback(){
    if (where == GRB_CB_MIPSOL) //integer solution
        solution_value = &CVRPCutsCallback::getSolution;
    else if ((where == GRB_CB_MIPNODE) && (getIntInfo(GRB_CB_MIPNODE_STATUS) == GRB_OPTIMAL)) //fractional solution
        solution_value = &CVRPCutsCallback::getNodeRel;
    else
        return;

    //count number of edges x_e > 0
    int nedges = 0;
    for(EdgeIt e(cvrp.g); e != INVALID; ++e){
        if((this ->*solution_value)(x[e]) > EpsForIntegrality)
            nedges++;
    }

    //populate EdgeTail, EdgeHead and EdgeX
    int *EdgeTail, *EdgeHead, i = 1;
    double *EdgeX;

    EdgeTail = new int[nedges + 1];
    EdgeHead = new int[nedges + 1];
    EdgeX = new double[nedges + 1];

    for(EdgeIt e(cvrp.g); e != INVALID; ++e){
        if((this ->*solution_value)(x[e]) > EpsForIntegrality){
            int u = cvrp.vname[cvrp.g.u(e)];
            if(u == 0)
                u = cvrp.n;

            int v = cvrp.vname[cvrp.g.v(e)];
            if(v == 0)
                v = cvrp.n;

            EdgeTail[i] = u;
            EdgeHead[i] = v;
            EdgeX[i] = (this ->*solution_value)(x[e]);
            i++;
        }
    }

    //call cvrpsep routine for finding the RCIs
    /*
    cout << "demand: ";
    for(i = 1; i < cvrp.n; i++)
        cout << " " << Demand[i];
    cout << endl;

    cout << "EdgeTail: ";
    for(i = 1; i < nedges; i++)
        cout << " " << EdgeTail[i];
    cout << endl;

    cout << "EdgeHead: ";
    for(i = 1; i < nedges; i++)
        cout << " " << EdgeHead[i];
    cout << endl;

    cout << "EdgeX: ";
    for(i = 1; i < nedges; i++)
        cout << " " << EdgeX[i];
    cout << endl;
    */

    //get capacity separation cuts
    CAPSEP_SeparateCapCuts(NoOfCustomers, Demand, CAP, nedges, EdgeTail, EdgeHead,
        EdgeX, MyOldCutsCMP,MaxNoOfCapCuts, EpsForIntegrality,
        &IntegerAndFeasible, &MaxCapViolation, MyCutsCMP);

    //Optimal solution found
    if (IntegerAndFeasible){
        //free edges arrays
        delete[] EdgeTail;
        delete[] EdgeHead;
        delete[] EdgeX;

        return;
    }

    //get framed capacity inequalities(FCI) cuts
    FCISEP_SeparateFCIs(NoOfCustomers, Demand, CAP, nedges, EdgeTail, EdgeHead,
        EdgeX, MyOldCutsCMP, MaxNoOfFCITreeNodes, MaxNoOfFCICuts, &MaxFCIViolation, MyCutsCMP);

    //get homogeneous multistar cuts
    MSTARSEP_SeparateMultiStarCuts(NoOfCustomers, Demand, CAP, nedges, EdgeTail, EdgeHead,
        EdgeX, MyOldCutsCMP, MaxNoOfMStarCuts, &MaxMStarViolation, MyCutsCMP);

    //get strengthened comb inequalities
    COMBSEP_SeparateCombs(NoOfCustomers, Demand, CAP, QMin, nedges, EdgeTail, EdgeHead,
        EdgeX, MaxNoOfCombCuts, &MaxCombViolation, MyCutsCMP);

    //free edges arrays
    delete[] EdgeTail;
    delete[] EdgeHead;
    delete[] EdgeX;

    //no cuts found
    if (MyCutsCMP -> Size == 0)
        return;

    //read the cuts from MyCutsCMP, and add them to the LP
    double RHS;
    for (i = 0; i < MyCutsCMP -> Size; i++){
        //capacity separation cuts
        if (MyCutsCMP->CPL[i]->CType == CMGR_CT_CAP){
            int ListSize = 0;
            int List[NoOfCustomers + 1];

            //populate List with the customers defining the cut
            for (int j = 1; j <= MyCutsCMP -> CPL[i] -> IntListSize; j++){
                int aux = MyCutsCMP -> CPL[i] -> IntList[j];

                if(aux == cvrp.n)
                    aux = 0;

                List[++ListSize] = aux;
            }

            //create the gurobi expression for x(S:S) <= |S| - k(S)
            GRBLinExpr expr = 0;

            //cout << "constraint: ";
            for(int j = 1; j <= ListSize; j++){
                for(int k = j + 1; k <= ListSize; k++){
                    Edge e = findEdge(cvrp.g, cvrp.g.nodeFromId(List[j]), cvrp.g.nodeFromId(List[k]));
                    //cout << " + x[" << List[j] << "][" << List[k] << "]";
                    expr += x[e];
                }
            }

            RHS = MyCutsCMP->CPL[i]->RHS;
            //cout << " <= " << RHS << endl;

            //add the cut to the LP
            addLazy(expr <= RHS);
        }

        //FCI cuts
        else if(MyCutsCMP->CPL[i]->CType == CMGR_CT_FCI){
            int MaxIdx = 0, MinIdx, k, w = 1;
            int nsubsets = MyCutsCMP->CPL[i]->ExtListSize;
            int sets_index[nsubsets + 1];
            int *sets[nsubsets + 1];
            int *S;

            //allocate memory
            S = new int[cvrp.n + 1];
            for (int SubsetNr = 1; SubsetNr <= nsubsets; SubsetNr++)
                sets[SubsetNr] = new int[cvrp.n + 1];

            for (int SubsetNr = 1; SubsetNr <= nsubsets; SubsetNr++){
                // (subset sizes are stored in ExtList)
                MinIdx = MaxIdx + 1;
                MaxIdx = MinIdx + MyCutsCMP->CPL[i]->ExtList[SubsetNr] - 1;

                sets_index[SubsetNr] = 1;
                for (int j = MinIdx; j <= MaxIdx; j++){
                    k = MyCutsCMP->CPL[i]->IntList[j];

                    //sets will store each vertex in the respective S_i
                    sets[SubsetNr][sets_index[SubsetNr]] = k;
                    sets_index[SubsetNr]++;

                    //S will store all vertexes in a single array
                    S[w] = k;
                    w++;
                }
            }

            //here we construct the expression for the RCI
            //note that the index will give the next free position, and therefore can be used as the size
            GRBLinExpr deltaS = getDeltaExpr(S, w);
            GRBLinExpr deltaSum = 0;
            for(int SubsetNr = 1; SubsetNr <= nsubsets; SubsetNr++)
                deltaSum += getDeltaExpr(sets[SubsetNr], sets_index[SubsetNr]);
            RHS = MyCutsCMP->CPL[i]->RHS;

            // Add the cut to the LP
            addLazy(deltaS + deltaSum >= RHS);

            //free memory
            delete[] S;
            for (int SubsetNr = 1; SubsetNr <= nsubsets; SubsetNr++)
                delete[] sets[SubsetNr];

        }

        //homogeneous multistar cuts
        else if (MyCutsCMP->CPL[i]->CType == CMGR_CT_MSTAR)
        {
            int A, B, L, sizeN, sizeT, sizeC;

            sizeN = MyCutsCMP->CPL[i]->IntListSize;
            sizeT = MyCutsCMP->CPL[i]->ExtListSize;
            sizeC = MyCutsCMP->CPL[i]->CListSize;

            int *NList, *TList, *CList;

            //allocate memory
            NList = new int[sizeN + 1];
            TList = new int[sizeT + 1];
            CList = new int[sizeC + 1];

            // Nucleus
            //cout << "N: ";
            for (int j=1; j<=MyCutsCMP->CPL[i]->IntListSize; j++){
                NList[j] = MyCutsCMP->CPL[i]->IntList[j];
                //cout << NList[j] << " ";
            }
            //cout << endl;

            // Satellites
            //cout << "T: ";
            for (int j=1; j<=MyCutsCMP->CPL[i]->ExtListSize; j++){
                TList[j] = MyCutsCMP->CPL[i]->ExtList[j];
                //cout << TList[j] << " ";
            }
            //cout << endl;

            // Connectors
            //cout << "C: ";
            for (int j=1; j<=MyCutsCMP->CPL[i]->CListSize; j++){
                CList[j] = MyCutsCMP->CPL[i]->CList[j];
                //cout << CList[j] << " ";
            }
            //cout << endl;

            // Coefficients of the cut:
            A = MyCutsCMP->CPL[i]->A;
            B = MyCutsCMP->CPL[i]->B;
            L = MyCutsCMP->CPL[i]->L;

            // Lambda=L/B, Sigma=A/B
            // Add the cut to the LP
            GRBLinExpr exprN = getDeltaExpr(NList, sizeN + 1);
            GRBLinExpr exprCT = getCrossingExpr(TList, CList, sizeT + 1, sizeC + 1);
            addLazy(B * exprN - A * exprCT >= L);

            //free memory
            delete[] NList;
            delete[] TList;
            delete[] CList;
        }

        //strengthened comb cuts
        if (MyCutsCMP->CPL[i]->CType == CMGR_CT_STR_COMB)
        {
            int NoOfTeeth = MyCutsCMP->CPL[i]->Key;
            int j;
            int *teeth[NoOfTeeth + 1];
            int *handle;
            int MinIdx, MaxIdx;
            int teeth_index[NoOfTeeth + 1];
            int handle_size = MyCutsCMP->CPL[i]->IntListSize;

            //allocate memory
            for (int t = 1; t <= NoOfTeeth; t++)
                teeth[t] = new int[cvrp.n + 1];
            handle = new int[cvrp.n + 1];

            //get handle
            //cout << "handle: ";
            for (int k = 1; k <= handle_size; k++){
                j = MyCutsCMP->CPL[i]->IntList[k];
                handle[k] = j;
                //cout << j << " ";
            }
            //cout << endl;

            //get teeth
            for (int t = 1; t <= NoOfTeeth; t++){
                MinIdx = MyCutsCMP->CPL[i]->ExtList[t];

                if (t == NoOfTeeth)
                    MaxIdx = MyCutsCMP->CPL[i]->ExtListSize;
                else
                    MaxIdx = MyCutsCMP->CPL[i]->ExtList[t + 1] - 1;

                teeth_index[t] = 1;
                //cout << "teeth[" << t << "] ";
                for (int k = MinIdx; k <= MaxIdx; k++){
                    j = MyCutsCMP->CPL[i]->ExtList[k];
                    // Node j is in tooth t
                    teeth[t][teeth_index[t]] = j;
                    teeth_index[t]++;
                    //cout << j << " ";
                }
                //cout << endl;
            }

            //get the expression
            GRBLinExpr handleExpr = getDeltaExpr(handle, handle_size + 1);

            GRBLinExpr teethExpr = 0;
            for (int t = 1; t <= NoOfTeeth; t++)
                teethExpr += getDeltaExpr(teeth[t], teeth_index[t]);

            RHS = MyCutsCMP->CPL[i]->RHS;

            //cout << "RHS " << RHS << endl;
            // Add the cut to the LP
            addLazy(handleExpr + teethExpr >= RHS);

            //free memory
            delete[] handle;
            for (int t = 1; t <= NoOfTeeth; t++)
                delete[] teeth[t];
        }
    }

    //move the new cuts to the list of old cuts
    for (i = 0; i < MyCutsCMP -> Size; i++){
        CMGR_MoveCnstr(MyCutsCMP, MyOldCutsCMP, i, 0);
    }

    MyCutsCMP->Size = 0;
}
