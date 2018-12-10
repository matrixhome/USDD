/**
 *  @file
 *  @copyright defined in eos/LICENSE.txt
 */

#include "usdd.hpp"

void usddtoken::create( account_name issuer, asset maximum_supply) {
    require_auth( _self );

    auto sym = maximum_supply.symbol;
    eosio_assert( sym.is_valid(), "invalid symbol name" );
    eosio_assert( maximum_supply.is_valid(), "invalid supply");
    eosio_assert( maximum_supply.amount > 0, "max-supply must be positive");

    stats statstable( _self, sym.name() );
    auto existing = statstable.find( sym.name() );
    eosio_assert( existing == statstable.end(), "token with symbol already exists" );

    statstable.emplace( _self, [&]( auto& s ) {
       s.supply.symbol = maximum_supply.symbol;
       s.max_supply    = maximum_supply;
       s.issuer        = issuer;
    });
}

void usddtoken::issue( account_name to, asset quantity, string memo ) {
    auto sym = quantity.symbol;
    eosio_assert( sym.is_valid(), "invalid symbol name" );
    eosio_assert( memo.size() <= 256, "memo has more than 256 bytes" );

    auto sym_name = sym.name();
    stats statstable( _self, sym_name );
    auto existing = statstable.find( sym_name );
    eosio_assert( existing != statstable.end(), "token with symbol does not exist, create token before issue" );
    const auto& st = *existing;

    require_auth( st.issuer );
    eosio_assert( quantity.is_valid(), "invalid quantity" );
    eosio_assert( quantity.amount > 0, "must issue positive quantity" );

    eosio_assert( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );
    eosio_assert( quantity.amount <= st.max_supply.amount - st.supply.amount, "quantity exceeds available supply");
    
    int64_t supply_amount = st.supply.amount;
    add_balance( st.issuer, quantity, st.issuer );
    statstable.modify( st, 0, [&]( auto& s ) {
       s.supply += quantity;
    });

    if( to != st.issuer ) {
       SEND_INLINE_ACTION( *this, transfer, {st.issuer,N(active)}, {st.issuer, to, quantity, memo} );
    } else {
       if ( memo == "claim" ) {
         add_interest(quantity, supply_amount, st.issuer);
       } else {
         distribute_interest(quantity, supply_amount, st.issuer);
       }
    }
}

void usddtoken::transfer( account_name from,
                      account_name to,
                      asset        quantity,
                      string       memo ) {

    eosio_assert( from != to, "cannot transfer to self." );
    require_auth( from );
    eosio_assert( is_account( to ), "to account does not exist.");
    auto sym = quantity.symbol.name();
    stats statstable( _self, sym );
    const auto& st = statstable.get( sym );

    require_recipient( from );
    require_recipient( to );

    eosio_assert( quantity.is_valid(), "invalid quantity" );
    eosio_assert( quantity.amount > 0, "must transfer positive quantity" );
    eosio_assert( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );
    eosio_assert( memo.size() <= 256, "memo has more than 256 bytes" );

    if( to == _self ) {
       eosio_assert( memo.size() == 42, "ETH Address is not right" );
       eosio_assert( memo.substr(0, 2) == "0x", "ETH Address is not right, please confirm" );
       sub_balance( from, quantity );
       statstable.modify( st, 0, [&]( auto& s ) {
          s.supply -= quantity;
       });
       sub_balance( from, quantity );
   } else {
       sub_balance( from, quantity );
       add_balance( to, quantity, from );
   }
}

void usddtoken::sub_balance( account_name owner, asset value ) {

   accounts from_acnts( _self, owner );

   const auto& from = from_acnts.get( value.symbol.name(), "no account object found" );
   eosio_assert( from.balance.amount >= value.amount, "overdrawn balance" );

   if( from.balance.amount == value.amount ) {
      from_acnts.erase( from );
   } else {
      from_acnts.modify( from, owner, [&]( auto& a ) {
          a.balance -= value;
      });
   }

   const auto&  user_itr = _users.find( owner);
   if( user_itr->balance.amount == value.amount ) {
      _users.erase( user_itr );
   } else {
      _users.modify( user_itr, 0, [&]( auto& a ) {
          a.balance -= value;
      });
   }
}

void usddtoken::add_balance( account_name owner, asset value, account_name ram_payer ) {

   accounts to_acnts( _self, owner );
   auto to = to_acnts.find( value.symbol.name() );
   if( to == to_acnts.end() ) {
      to_acnts.emplace( ram_payer, [&]( auto& a ){
        a.balance = value;
      });
   } else {
      to_acnts.modify( to, 0, [&]( auto& a ) {
        a.balance += value;
      });
   }

   auto user_itr = _users.find( owner );
   if( user_itr == _users.end() ) {
      eosio::asset interest;
      interest.amount = 0;
      interest.symbol = USDD_SYMBOL;

      _users.emplace( ram_payer, [&]( auto& a ){
        a.balance = value;
        a.owner = owner;
        a.interest = interest;
      });
   } else {
      _users.modify( user_itr, 0, [&]( auto& a ) {
        a.balance += value;
      });
   }
}

void usddtoken::distribute_interest( asset interests, int64_t supply, account_name issuer ) {
   require_auth( _self );
   eosio_assert( interests.amount <= supply, "interest big than supply, it's wrong." );
   eosio_assert( interests.amount <= 999999999, "interest is to big, it's wrong." );

   int64_t ibalance = interests.amount;
   for(auto& item : _users) {
      if (item.owner == _self) {
         continue;
      }
      eosio_assert( item.balance.amount <= supply, "user balance big than supply, it's wrong" );
      int64_t interest = item.balance.amount * interests.amount / supply;
      if (interest >= _baseline) {
         eosio::asset quality;
         quality.amount = interest;
         quality.symbol = USDD_SYMBOL;

         SEND_INLINE_ACTION( *this, transfer, {issuer,N(active)}, {issuer, item.owner, quality, ""} );
         ibalance = ibalance - interest;
      }
   }

   if (ibalance > 0) {
      eosio::asset balance_qty;
      balance_qty.amount = ibalance;
      balance_qty.symbol = USDD_SYMBOL;
      account_name piggy_bank = eosio::string_to_name(_bank);
      SEND_INLINE_ACTION( *this, transfer, {issuer,N(active)}, {issuer, piggy_bank, balance_qty, ""} );
   }
}

void usddtoken::add_interest( asset interests, int64_t supply, account_name issuer ) {
   require_auth( _self );
   eosio_assert( interests.amount <= supply, "interest big than supply, it's wrong" );
   eosio_assert( interests.amount <= 999999999, "interest is too big, it's wrong" );

   int64_t ibalance = interests.amount;
   for(auto& item : _users) {
      if (item.owner == _self) {
         continue;
      }
      eosio_assert( item.balance.amount <= supply, "user balance big than supply, it's wrong" );
      int64_t interest = item.balance.amount * interests.amount / supply;
      if (interest >= _baseline) {
         eosio::asset interest_qty;
         interest_qty.amount = interest;
         interest_qty.symbol = USDD_SYMBOL;
         _users.modify( item, 0, [&]( auto& a ) {
            a.interest += interest_qty;
         });
         ibalance = ibalance - interest;
      }
   }

   if (ibalance > 0) {
      eosio::asset balance_qty;
      balance_qty.amount = ibalance;
      balance_qty.symbol = USDD_SYMBOL;
      account_name piggy_bank = eosio::string_to_name(_bank);
      SEND_INLINE_ACTION( *this, transfer, {issuer,N(active)}, {issuer, piggy_bank, balance_qty, ""} );
   }
}

void usddtoken::claim( account_name owner ) {
    eosio_assert( owner != _self, "cannot claim to self" );
    require_auth( owner );
    eosio_assert( is_account( owner ), "to account does not exist");

   auto user_itr = _users.find( owner );
   if( user_itr != _users.end() ) {
      eosio::asset interest = user_itr->interest;
      eosio_assert( interest.amount >= _baseline, "Your interest is zero");

      SEND_INLINE_ACTION( *this, transfer, {_self, N(active)}, {_self, owner, interest, ""} );
      _users.modify( user_itr, 0, [&]( auto& a ) {
         a.interest -= interest;
      });
   }
}

EOSIO_ABI( usddtoken, (create)(issue)(transfer)(claim) )

