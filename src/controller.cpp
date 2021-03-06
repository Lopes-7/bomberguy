#include <chrono>
#include <thread>

#include "controller.hpp"

Controller::Controller(Player *player, int size_y, int size_x, int enemy_count)
{
    this->game_status = true;
    this->player = player;
    this->map = new Map(size_y, size_x);
    this->screen = new Screen(this->map, player);

    Position player_pos = player->get_pos();
    this->map->set_flag(std::get<0>(player_pos), std::get<1>(player_pos), FLAG_PLAYER);

    this->screen->loading_page();

    /* Load all audio samples */
    this->bg_audio.load_sample(AUDIO_BACKGROUND_MUSIC);
    this->bg_audio.load_sample(AUDIO_GAMEOVER_MUSIC);
    this->sfx_audio.load_sample(AUDIO_BOMB_DROP);
    this->sfx_audio.load_sample(AUDIO_EXPLOSION, 0.5);
    this->sfx_audio.load_sample(AUDIO_DOOR_DISCOVER);
    this->sfx_audio.load_sample(AUDIO_POWER_UP);

    this->bg_audio.play(AUDIO_BACKGROUND_MUSIC);

    this->screen->start_map_screen();

    while (enemy_count-- > 0) {
        this->spawn_enemy(100);
    }
}

Controller::~Controller()
{
    for (std::vector<Bomb *>::iterator it = this->bomb_list.begin(); it != this->bomb_list.end();) {
        delete (*it);
        it = this->bomb_list.erase(it);
    }

    for (std::vector<Enemy *>::iterator it = this->enemy_list.begin(); it != this->enemy_list.end();) {
        delete (*it);
        it = this->enemy_list.erase(it);
    }

    delete this->screen;
    delete this->map;
}

int Controller::drop_bomb(int remaining_time)
{
    Bomb *bomb;
    Position pos = this->player->get_pos();

    if (this->player->get_bomb_count() <= 0) {
        /* Not enough bombs stored */
        return 0;
    }

    /* There's already a bomb in this tile */
    if (this->map->has_flag(std::get<0>(pos), std::get<1>(pos), FLAG_BOMB)) {
        return 0;
    }

    /* Decrement player bomb count */
    this->player->remove_bomb();

    /* Include bomb on the bomb-list to be handled by the controller */
    bomb = new Bomb(pos, remaining_time, this->player->get_bomb_range());
    this->bomb_list.push_back(bomb);

    /* Set the bomb on the map */
    this->map->set_flag(std::get<0>(pos), std::get<1>(pos), FLAG_BOMB);

    /* Play sound effect */
    this->sfx_audio.play(AUDIO_BOMB_DROP);
    return 1;
}

Position Controller::move_player(Direction dir)
{
    Position old_pos = this->player->get_pos();

    Position ret_pos = old_pos;

    int old_y = std::get<0>(old_pos);
    int old_x = std::get<1>(old_pos);

    int new_y = old_y + dir_y[dir];
    int new_x = old_x + dir_x[dir];

    /* Check if player is not trying to move into a 'untrespassable' terrain */
    if (this->map->is_walkable(new_y, new_x)) {
        this->map->clear_flag(old_y, old_x, FLAG_PLAYER);
        this->map->set_flag(new_y, new_x, FLAG_PLAYER);
        this->player->set_pos(std::make_tuple(new_y, new_x));
        ret_pos = this->player->get_pos();
    }

    return ret_pos;
}

void Controller::spawn_enemy(int pos_y, int pos_x, double vel_y, double vel_x, int score)
{
    Enemy * enemy = new Enemy(std::make_tuple(pos_y,pos_x), std::make_tuple(vel_y, vel_x), score);

    this->enemy_list.push_back(enemy);

    this->map->set_flag(pos_y, pos_x, FLAG_ENEMY);
}

void Controller::spawn_enemy(int score)
{
    int x, y;
    double vel_x = 0, vel_y=0;
    do {
        x = rand() % this->map->get_size_x();
        y = rand() % this->map->get_size_y();

    } while (!((x > 2 || y > 2) && this->map->is_empty(y, x)));

    if (((y % 2) == 0) && (x % 2)) {
        vel_x = 0;
        vel_y = -1;
    } else {
        vel_x = -1;
        vel_y = 0;
    }

    Enemy * enemy = new Enemy(std::make_tuple(y, x), std::make_tuple(vel_y, vel_x), score);

    this->enemy_list.push_back(enemy);

    this->map->set_flag(y, x, FLAG_ENEMY);
}

Position_d Controller::move_enemy(Enemy *enemy, Position_d new_pos)
{
    Velocity vel = enemy->get_velocity();

    Position_d old_pos = enemy->get_pos();
    Position_d ret_pos = old_pos;

    int old_y = floor(std::get<0>(old_pos));
    int old_x = floor(std::get<1>(old_pos));

    int new_y = floor(std::get<0>(new_pos));
    int new_x = floor(std::get<1>(new_pos));

    /* Check if enemy is not trying to move into a 'untrespassable' terrain */
    if (this->map->is_walkable(new_y, new_x)) {
        this->map->clear_flag(old_y, old_x, FLAG_ENEMY);
        this->map->set_flag(new_y, new_x, FLAG_ENEMY);
        enemy->set_pos(new_pos);
        ret_pos = new_pos;
    } else {
        /* Reverse direction */
        std::get<0>(vel) *= -1;
        std::get<1>(vel) *= -1;
        enemy->set_velocity(vel);
    }

    return ret_pos;
}


void Controller::check_colisions(void)
{
    /* Iterate over all blocks on map */
    for (int y = 0; y < this->map->get_size_y(); y++) {
        for (int x = 0; x < this->map->get_size_x(); x++) {
            /* Check if player touched a dangerous object */
            if (this->map->has_flag(y, x, FLAG_PLAYER) &&
                (this->map->has_flag(y, x, FLAG_FLAME) || this->map->has_flag(y, x, FLAG_ENEMY))) {
                this->kill_player(y, x);
            }

            /* Break brick */
            if (this->map->has_flag(y, x, FLAG_FLAME) && this->map->has_flag(y, x, FLAG_BRICK)) {
                this->map->clear_flag(y, x, FLAG_BRICK);
            }

            /* Bomb chain-effect */
            if (this->map->has_flag(y, x, FLAG_FLAME) && this->map->has_flag(y, x, FLAG_BOMB)) {
                this->explode_bomb(this->find_bomb(std::make_tuple(y, x)));
            }

            /* Please don't explode the walls, thank you */
            if (this->map->has_flag(y, x, FLAG_FLAME) && this->map->has_flag(y, x, FLAG_WALL)) {
                this->map->clear_flag(y, x, FLAG_FLAME);
            }

            /* Kill them with fire! */
            if (this->map->has_flag(y, x, FLAG_FLAME) && this->map->has_flag(y, x, FLAG_ENEMY)) {
                this->map->clear_flag(y, x, FLAG_ENEMY);
                this->kill_enemy(std::make_tuple(y, x));
            }

            /* Check if the player can leave the game  */
            if (this->map->has_flag(y, x, FLAG_PLAYER) && this->map->has_flag(y, x, FLAG_DOOR)) {
                if (this->enemy_list.size() == 0) {
                    this->set_game_status(false);
                }
            }

            if (this->map->has_flag(y, x, FLAG_FLAME) && this->map->has_flag(y, x, FLAG_DOOR)) {
		if (this->map->door_found == 0) {
		    this->sfx_audio.play(AUDIO_DOOR_DISCOVER);
		    this->map->door_found = 1;
		}
            }

            /* Power up */
            if (this->map->has_flag(y, x, FLAG_PLAYER) && this->map->has_flag(y, x, FLAG_PWR_BOMB)) {
                this->map->clear_flag(y, x, FLAG_PWR_BOMB);
                this->player->set_max_bombs(this->player->get_max_bombs() + 1);
                this->player->set_score(this->player->get_score() + 50);
                this->sfx_audio.play(AUDIO_POWER_UP);
            }
            /* Power up */
            if (this->map->has_flag(y, x, FLAG_PLAYER) && this->map->has_flag(y, x, FLAG_PWR_FLAME)) {
                this->map->clear_flag(y, x, FLAG_PWR_FLAME);
                this->player->set_bomb_range(this->player->get_bomb_range() + 1);
		this->player->set_score(this->player->get_score() + 50);
                this->sfx_audio.play(AUDIO_POWER_UP);
            }

            /* Power up */
            if (this->map->has_flag(y, x, FLAG_PLAYER) && this->map->has_flag(y, x, FLAG_PWR_LIFE)) {
                this->map->clear_flag(y, x, FLAG_PWR_LIFE);
                this->player->set_lives(this->player->get_lives() + 1);
		this->player->set_score(this->player->get_score() + 50);
                this->sfx_audio.play(AUDIO_POWER_UP);
            }
        }
    }

}

void Controller::update(double deltaT)
{
    /************ Bomb Handling ************/

    /* Update time for all bombs */
    for (std::vector<Bomb *>::iterator it = this->bomb_list.begin(); it != this->bomb_list.end(); it++) {
        (*it)->set_remaining_time((*it)->get_remaining_time() - deltaT);
    }

    /* Update bombs status */
    for (std::vector<Bomb *>::iterator it = this->bomb_list.begin(); it != this->bomb_list.end(); it++) {
        int rem_time = (*it)->get_remaining_time();
        if (rem_time <= 0) {
            explode_bomb(*it);
        } else if (rem_time <= 1000) {
            /* large bomb icon */
        } else if (rem_time <= 2000) {
            /* small bomb icon */
        }
    }

    /* Clean-up bombs */
    for (std::vector<Bomb *>::iterator it = this->bomb_list.begin(); it != this->bomb_list.end();) {
        if ((*it)->get_status() == BOMB_REMOVE) {
            delete (*it);
            it = this->bomb_list.erase(it);
            this->player->add_bomb();
        } else {
            it++;
        }
    }

    /************ Enemy Handling ************/
    for (std::vector<Enemy *>::iterator e = this->enemy_list.begin(); e != this->enemy_list.end(); e++) {
        Velocity vel;
        Position_d pos;

        vel = (*e)->get_velocity();
        pos = (*e)->get_pos();

        double pos_x = std::get<0>(pos);
        double pos_y = std::get<1>(pos);
        double vel_x = std::get<0>(vel);
        double vel_y = std::get<1>(vel);

        pos_x += (double)(vel_x * (deltaT/1000));
        pos_y += (double)(vel_y * (deltaT/1000));

        pos = this->move_enemy(*e, std::make_tuple(pos_x, pos_y));

    }

    /* Proccess all the new positions */
    this->check_colisions();

    /* Update screen */
    this->screen->update();
}

Bomb *Controller::find_bomb(Position f_pos)
{
    return *(std::find_if(this->bomb_list.begin(), this->bomb_list.end(),
                          [f_pos]( Bomb* const b) -> bool { return (b->get_pos() == f_pos); }));
}

void Controller::explode_bomb(Bomb *bomb)
{
    Position pos = bomb->get_pos();
    Position explosion_pos;
    int y = std::get<0>(pos);
    int x = std::get<1>(pos);
    int dir, range;

    this->map->clear_flag(y, x, FLAG_BOMB);

    for (dir = 0; dir < 4; dir++) {
        for (range = 0; range < bomb->get_range(); range++) {
            x = std::get<1>(pos) + dir_x[dir]*range;
            y = std::get<0>(pos) + dir_y[dir]*range;
            explosion_pos = std::make_tuple(y, x);

            if (!this->map->is_valid_pos(y, x)) {
                continue;
            }

            if (bomb->get_status() == BOMB_ARMED) {
                this->map->set_flag(y, x, FLAG_FLAME);

                /* Don't allow the explosion to get past 'explosive' things */
                if (this->map->has_flag(y, x, FLAG_BRICK) ||
                    this->map->has_flag(y, x, FLAG_WALL) ||
                    this->map->has_flag(y, x, FLAG_BOMB) ||
                    this->map->has_flag(y, x, FLAG_ENEMY) ||
                    this->map->has_flag(y, x, FLAG_DOOR) ||
                    this->map->has_flag(y, x, FLAG_PLAYER) ) {
                    range = bomb->get_range();
                }

            } else if (bomb->get_status() == BOMB_EXPLODED) {
                this->map->clear_flag(y, x, FLAG_FLAME);
                /* Don't allow the explosion to get past 'explosive' things */
                if (this->map->has_flag(y, x, FLAG_BRICK) ||
                    this->map->has_flag(y, x, FLAG_WALL) ||
                    this->map->has_flag(y, x, FLAG_BOMB) ||
                    this->map->has_flag(y, x, FLAG_ENEMY) ||
                    this->map->has_flag(y, x, FLAG_DOOR) ||
                    this->map->has_flag(y, x, FLAG_PLAYER) ) {
                    range = bomb->get_range();
                }
            }
        }
    }

    if (bomb->get_status() == BOMB_ARMED) {
        /* Play sound effect */
        this->sfx_audio.play(AUDIO_EXPLOSION);
        bomb->set_status(BOMB_EXPLODED);
        bomb->set_remaining_time(EXPLOSION_WEAROFF_TIME);
    } else if (bomb->get_status() == BOMB_EXPLODED){
        bomb->set_status(BOMB_REMOVE);
    }
}

void Controller::remove_bomb(Bomb *bomb)
{
    std::vector<Bomb *>::iterator bomb_vec_pos = std::find(this->bomb_list.begin(), this->bomb_list.end(), bomb);
    if (bomb_vec_pos != this->bomb_list.end()) {
        delete (*bomb_vec_pos);
        this->bomb_list.erase(bomb_vec_pos);
    }
}

void Controller::kill_enemy(Position pos)
{
    for (std::vector<Enemy *>::iterator it = this->enemy_list.begin(); it != this->enemy_list.end();) {
        int y = floor(std::get<0>((*it)->get_pos()));
        int x = floor(std::get<1>((*it)->get_pos()));
        if (std::make_tuple(y,x) == pos) {
            this->player->set_score(this->player->get_score() + (*it)->get_score());
            delete (*it);
            it = this->enemy_list.erase(it);
        } else {
            it++;
        }
    }
}

void Controller::kill_player(int y, int x)
{
    if (this->player->get_lives() == 1) {
        this->set_game_status(false);
	this->sfx_audio.pause();
	this->screen->update();
	this->bg_audio.play(AUDIO_GAMEOVER_MUSIC);
	std::this_thread::sleep_for (std::chrono::milliseconds(4000));
    } else {
        this->player->set_lives(this->player->get_lives()-1);
        /* Teleport player to the start position */
        this->map->clear_flag(y, x, FLAG_PLAYER);
        this->map->set_flag(1, 1, FLAG_PLAYER);
        this->player->set_pos(std::make_tuple(1,1));
    }
}

bool Controller::get_game_status(void)
{
    return this->game_status;
}

void Controller::set_game_status(bool sts)
{
    this->game_status = sts;
}
