#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <inttypes.h>

#include "teller.h"
#include "account.h"
#include "branch.h"
#include "error.h"
#include "debug.h"

/*
 * deposit money into an account
 */
int
Teller_DoDeposit(Bank *bank, AccountNumber accountNum, AccountAmount amount)
{
  assert(amount >= 0);

  DPRINTF('t', ("Teller_DoDeposit(account 0x%"PRIx64" amount %"PRId64")\n",
                accountNum, amount));

  Account *account = Account_LookupByNumber(bank, accountNum);

  if (account == NULL) {
    return ERROR_ACCOUNT_NOT_FOUND;
  }

  sem_wait(&(account->lock));
  sem_wait(&(bank->branches[AccountNum_GetBranchID(accountNum)].lock));

  Account_Adjust(bank,account, amount, 1);

  sem_post(&(account->lock));
  sem_post(&(bank->branches[AccountNum_GetBranchID(accountNum)].lock));

  return ERROR_SUCCESS;
}

/*
 * withdraw money from an account
 */
int
Teller_DoWithdraw(Bank *bank, AccountNumber accountNum, AccountAmount amount)
{
  assert(amount >= 0);

  DPRINTF('t', ("Teller_DoWithdraw(account 0x%"PRIx64" amount %"PRId64")\n",
                accountNum, amount));

  Account *account = Account_LookupByNumber(bank, accountNum);

  if (account == NULL) {
    return ERROR_ACCOUNT_NOT_FOUND;
  }

  sem_wait(&(account->lock));
  sem_wait(&(bank->branches[AccountNum_GetBranchID(accountNum)].lock));

  if (amount > Account_Balance(account)) {
    sem_post(&(account->lock));
    sem_post(&(bank->branches[AccountNum_GetBranchID(accountNum)].lock));

    return ERROR_INSUFFICIENT_FUNDS;
  }

  Account_Adjust(bank,account, -amount, 1);

  sem_post(&(account->lock));
  sem_post(&(bank->branches[AccountNum_GetBranchID(accountNum)].lock));

  return ERROR_SUCCESS;
}

/*
 * do a tranfer from one account to another account
 */
int
Teller_DoTransfer(Bank *bank, AccountNumber srcAccountNum,
                  AccountNumber dstAccountNum,
                  AccountAmount amount)
{
  assert(amount >= 0);

  DPRINTF('t', ("Teller_DoTransfer(src 0x%"PRIx64", dst 0x%"PRIx64
                ", amount %"PRId64")\n",
                srcAccountNum, dstAccountNum, amount));

  Account *srcAccount = Account_LookupByNumber(bank, srcAccountNum);
  if (srcAccount == NULL) {
    return ERROR_ACCOUNT_NOT_FOUND;
  }

  Account *dstAccount = Account_LookupByNumber(bank, dstAccountNum);
  if (dstAccount == NULL) {
    return ERROR_ACCOUNT_NOT_FOUND;
  }

  if (Account_IsSameBranch(srcAccountNum, dstAccountNum)) {
    if (srcAccount->accountNumber < dstAccount->accountNumber) {
      sem_wait(&(srcAccount->lock));
      sem_wait(&(dstAccount->lock));
    } else if (srcAccount->accountNumber > dstAccount->accountNumber) {
      sem_wait(&(dstAccount->lock));
      sem_wait(&(srcAccount->lock));
    } else if (srcAccount->accountNumber == dstAccount->accountNumber) {
      return ERROR_SUCCESS;
    }

    if (amount > Account_Balance(srcAccount)) {
      sem_post(&(srcAccount->lock));
      sem_post(&(dstAccount->lock));

      return ERROR_INSUFFICIENT_FUNDS;
    }

    /*
    * If we are doing a transfer within the branch, we tell the Account module to
    * not bother updating the branch balance since the net change for the
    * branch is 0.
    */
    int updateBranch = !Account_IsSameBranch(srcAccountNum, dstAccountNum);

    Account_Adjust(bank, srcAccount, -amount, updateBranch);
    Account_Adjust(bank, dstAccount, amount, updateBranch);

    sem_post(&(srcAccount->lock));
    sem_post(&(dstAccount->lock));

    return ERROR_SUCCESS;
  }
  
  if (AccountNum_GetBranchID(srcAccountNum) < AccountNum_GetBranchID(dstAccountNum)) {
    sem_wait(&(srcAccount->lock));
    sem_wait(&(dstAccount->lock));
    sem_wait(&(bank->branches[AccountNum_GetBranchID(srcAccountNum)].lock));
    sem_wait(&(bank->branches[AccountNum_GetBranchID(dstAccountNum)].lock));
  } else {
    sem_wait(&(dstAccount->lock));
    sem_wait(&(srcAccount->lock));
    sem_wait(&(bank->branches[AccountNum_GetBranchID(dstAccountNum)].lock));
    sem_wait(&(bank->branches[AccountNum_GetBranchID(srcAccountNum)].lock));
  } 

  if (amount > Account_Balance(srcAccount)) {
    sem_post(&(srcAccount->lock));
    sem_post(&(dstAccount->lock));
    sem_post(&(bank->branches[AccountNum_GetBranchID(srcAccountNum)].lock));
    sem_post(&(bank->branches[AccountNum_GetBranchID(dstAccountNum)].lock));

    return ERROR_INSUFFICIENT_FUNDS;
  }

  /*
  * If we are doing a transfer within the branch, we tell the Account module to
  * not bother updating the branch balance since the net change for the
  * branch is 0.
  */
  int updateBranch = !Account_IsSameBranch(srcAccountNum, dstAccountNum);

  Account_Adjust(bank, srcAccount, -amount, !updateBranch);
  Account_Adjust(bank, dstAccount, amount, !updateBranch);

  sem_post(&(srcAccount->lock));
  sem_post(&(dstAccount->lock));
  sem_post(&(bank->branches[AccountNum_GetBranchID(srcAccountNum)].lock));
  sem_post(&(bank->branches[AccountNum_GetBranchID(dstAccountNum)].lock));

  return ERROR_SUCCESS;
}
