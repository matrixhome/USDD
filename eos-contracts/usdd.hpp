/**
 *  @file
 *  @copyright defined in eos/LICENSE.txt
 */
#pragma once

#include <eosiolib/asset.hpp>
#include <eosiolib/eosio.hpp>

#include <string>
#define USDD_SYMBOL S(4, USDD)

namespace eosiosystem {
   class system_contract;
}

using std::string;
using eosio::asset;
using eosio::symbol_name;

class usddtoken : public eosio::contract {
  public:
	 usddtoken( account_name self ):contract(self), _users(self, _self){}

	 void create( account_name issuer,
				  asset        maximum_suppl);

	 void issue( account_name to, asset quantity, string memo );

	 void transfer( account_name from,
					account_name to,
					asset        quantity,
					string       memo );
  
     void claim( account_name owner );

	 inline asset get_supply( symbol_name sym )const;

	 inline asset get_balance( account_name owner, symbol_name sym )const;

	 int64_t _baseline = 1;
     char _bank[13] = "usdpiggybank";

  private:
     // @abi table accounts i64
	 struct account {
		asset    balance;
		uint64_t primary_key()const { return balance.symbol.name(); }
	 };

     // @abi table stat i64
	 struct cstats {
		asset          supply;
		asset          max_supply;
		account_name   issuer;

		uint64_t primary_key()const { return supply.symbol.name(); }
	 };

	 // @abi table users i64
	 struct user {
		account_name owner; 
		asset    balance;
		asset interest;
		uint64_t primary_key()const { return owner; }
		EOSLIB_SERIALIZE(user, (owner)(balance)(interest))
	 };

	 typedef eosio::multi_index<N(accounts), account> accounts;
	 typedef eosio::multi_index<N(stat), cstats> stats;
	 typedef eosio::multi_index<N(users), user> users;
	 users _users;

	 void sub_balance( account_name owner, asset value );
	 void add_balance( account_name owner, asset value, account_name ram_payer );
	 void distribute_interest( asset interests, int64_t supply, account_name issuer );
	 void add_interest( asset interests, int64_t supply, account_name issuer );

  public:
	 struct transfer_args {
		account_name  from;
		account_name  to;
		asset         quantity;
		string        memo;
	 };
};

asset usddtoken::get_supply( symbol_name sym )const {
  stats statstable( _self, sym );
  const auto& st = statstable.get( sym );
  return st.supply;
}

asset usddtoken::get_balance( account_name owner, symbol_name sym )const {
  accounts accountstable( _self, owner );
  const auto& ac = accountstable.get( sym );
  return ac.balance;
}
